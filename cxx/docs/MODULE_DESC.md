# Module Descriptions: Musical Toolbox

This document defines the functional requirements for all DSP processors. It is the
source of truth for port names, port types, parameter declarations, and connection rules.
All C++ implementations, C API functions, and patch files are governed by this document.

---

## Port Type System

All modules declare their ports using two types:

- **`PORT_AUDIO`** — an audio-rate signal carrying sound. Range: `[-1.0, 1.0]`. Connects only to `PORT_AUDIO` inputs.
- **`PORT_CONTROL`** — an audio-rate signal carrying modulation or CV. Connects only to `PORT_CONTROL` inputs.

Both port types run at **audio rate** — a full `std::span<float>` per block. The distinction is semantic and enforced at `bake()` time.

### Control Value Conventions

`PORT_CONTROL` ports follow one of two range conventions, declared per port:

- **Unipolar** `[0.0, 1.0]` — used for gain, envelope output, drawbar levels, and any quantity that cannot be negative.
- **Bipolar** `[-1.0, 1.0]` — used for LFO output, pitch CV, attenuverter output, and any quantity that can invert.

Pitch CV follows the **1V/oct convention scaled to float**: each `1.0` unit represents one octave (a doubling of frequency).

Each port descriptor includes a `unipolar` flag. Mismatching a unipolar source to a bipolar destination (or vice versa) is a patch authoring error, not a type error — `bake()` does not reject it, but the connection semantics must be documented in the patch.

### Lifecycle Ports (Gate & Trigger)

Certain port names are **reserved lifecycle ports**. They are driven by the `VoiceContext` (note_on/note_off events) and must not be wired via `connections_`. `bake()` **rejects** (throws `std::logic_error`) any explicit connection that references these port names:

| Reserved port name | Signal |
|--------------------|--------|
| `gate_in`          | High while note is held, low on note_off |
| `trigger_in`       | One-block pulse on note_on |

### Feedback Connections

A connection that forms a cycle in the signal graph (e.g. delay feedback) must be marked `"feedback": true` in the patch. Non-feedback connections form a DAG validated by `bake()`.

> **Current status**: Graph-level cross-node feedback is **not implemented**. The `"feedback": true` field in connections is parsed by the patch loader but the graph executor does not maintain a previous-block output buffer for cycle-breaking. `ECHO_DELAY`'s self-feedback is handled internally via a circular delay buffer inside the processor — not via graph executor feedback. Graph-level feedback is a future feature.

### Port Declaration in C++

Each processor subclass calls `declare_port()` in its constructor for every port it owns:

```cpp
declare_port({"envelope_out", PORT_CONTROL, OUT, UNIPOLAR});
declare_port({"gate_in",      PORT_CONTROL, IN,  UNIPOLAR}); // lifecycle — not wired via connections_
```

The `ModuleRegistry` singleton collects these declarations at library load time via static initializers in each processor `.cpp` file.

---

## Parameter Declaration

Alongside ports, each module declares its **parameters** — the knobs and switches a host
exposes to the user. Parameters are queryable via the C API.

```cpp
struct ParameterDescriptor {
    std::string name;        // "cutoff", "attack_time"
    std::string label;       // human-readable: "Cutoff Frequency"
    float min, max, def;     // range and default
    bool  logarithmic;       // true for frequency/time params
    std::string description; // usage note (Phase 26) — default ""
};

struct PortDescriptor {
    std::string   name;        // "audio_in", "gain_cv"
    PortType      type;        // PORT_AUDIO or PORT_CONTROL
    PortDirection dir;         // IN or OUT
    bool          unipolar;    // true → [0,1]; false → [-1,1] (PORT_CONTROL only)
    std::string   description; // usage note (Phase 26) — default ""
};
```

Each processor calls `declare_parameter()` and `declare_port()` in its constructor. The `description` field is optional (defaults to `""`) and is used by the Phase 27A introspection API to populate JSON module descriptors for host applications.

### Parameter Smoothing (Phase 21)

Continuous parameters (cutoff, gain, LFO rate/depth, FX wet/dry) use `SmoothedParam` members in the processor implementation. `set_target(value, ramp_samples)` schedules a linear ramp; the processor calls `advance(block_size)` at the start of each `do_pull` then reads `get()` for the block's value. Default ramp = `sample_rate × 0.010` (10 ms).

Discrete selectors (waveform type, transpose semitones, mode) and patch-configuration values set before audio starts (delay time, room size) use snap mode: `set_target(value, 0)` sets the value immediately. CV accumulator ports (`cutoff_cv`, `kybd_cv`, `res_cv`) are not smoothed — they are already smooth from their source.

---

## Module Registry

At library load time each processor `.cpp` registers its module type with `ModuleRegistry::instance()` via a static initializer:

```cpp
// Phase 26 extended signature: type_name, brief, usage_notes, factory
static const bool kReg = (ModuleRegistry::instance().register_module(
    "MOOG_FILTER",
    "4-pole Moog transistor ladder LP (24 dB/oct) — smooth, creamy, thick",
    "resonance > 0.8 approaches self-oscillation. Connect ENV → cutoff_cv for filter sweep. "
    "fm_in enables audio-rate cutoff modulation.",
    [](int sr) { return std::make_unique<MoogLadderProcessor>(sr); }
), true);
```

`ModuleRegistry` builds a prototype at `sample_rate=48000` to harvest the declared ports and parameters, stores everything in a `ModuleDescriptor`, then discards the prototype. The resulting `ModuleDescriptor` contains:

```cpp
struct ModuleDescriptor {
    std::string type_name;    // "MOOG_FILTER"
    std::string description;  // one-line brief
    std::string usage_notes;  // longer-form usage guidance (Phase 26)
    std::vector<PortDescriptor>      ports;
    std::vector<ParameterDescriptor> parameters;
    FactoryFn   factory;
};
```

The registry is queryable via the C API (`engine_get_module_count`, `engine_get_module_type`, `engine_get_module_port`, `engine_get_module_parameter`). See BRIDGE_GUIDE.md §5. The Phase 27A introspection API (`module_get_descriptor_json`, `module_registry_get_all_json`) exposes the full descriptor including `usage_notes` and per-port/parameter `description` fields as JSON.

---

## 1. Generators (Oscillators & Noise)

### VCO (Voltage Controlled Oscillator)
- **Type name**: `COMPOSITE_GENERATOR`
- **Purpose**: Primary periodic harmonic generation. Owns all waveform oscillators and an internal waveform mixer (gain-blended, not wirable — use `AUDIO_MIXER` for cross-VCO mixing).
- **Ports**:
  - `PORT_CONTROL` in `pitch_cv` (bipolar, 1V/oct)
  - `PORT_CONTROL` in `pwm_cv` (bipolar — LFO-style pulse-width modulation; formula: `pw = clamp(0.5 + cv × 0.5, 0.01, 0.49)`)
  - `PORT_CONTROL` in `pw_env_cv` (unipolar 0–1 — envelope-driven pulse-width narrowing; formula: `eff_pw = clamp(pulse_width − cv × pw_env_depth, 0.01, 0.49)`. Pulse narrows as the envelope rises. Use for bow-contact PWM character (Roland Fig 2-10); cleared each block after use.)
  - `PORT_AUDIO` in `fm_in` (audio-rate frequency modulation; a VCO `audio_out` patched here is multiplied by `fm_depth` and summed into the pitch CV path, enabling VCO-to-VCO FM synthesis and extreme pitch sweeps not achievable at control rate)
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: `saw_gain`, `pulse_gain`, `sine_gain`, `triangle_gain`, `sub_gain`, `wavetable_gain`, `noise_gain` (all 0.0–1.0), `pulse_width` (0.0–0.5), `pw_env_depth` (0.0–0.49, default 0.0 — scales how much `pw_env_cv` narrows the pulse width; 0 = no effect, 0.49 = full range), `wavetable_type` (int enum), `footage` (Roland range selector: 2/4/8/16/32, default 8 — see Footage convention below), `detune` (float cents, −100–+100, default 0.0), `fm_depth` (0.0–1.0, default 0.0 — scales the `fm_in` signal before summing with `pitch_cv`)
- **Logic**:
  - **Exponential Pitch**: Frequency response must follow an exponential curve (f = f₀ · 2^CV) corresponding to the standard 1V/octave tracking.
  - **Harmonic Spectra Rules**:
    - **Sawtooth**: Must contain all harmonics (even and odd). Amplitude decreases at a rate of 1/n (where n is the harmonic number). Waveform inversion (ramp-up vs. ramp-down) is permitted as it does not alter perceived tone color.
    - **Square**: Must contain only odd-numbered harmonics. Amplitude decreases at a rate of 1/n.
    - **Triangle**: Must contain only odd-numbered harmonics, with amplitudes decreasing sharply at a rate of 1/n².
  - All waveforms produced simultaneously, blended by the internal waveform mixer via per-gain parameters. Sub-oscillator phase-coupled to pulse oscillator.
  - **Pitch offset**: The final frequency is computed as `f = f_base * 2^(detune/1200) * 2^(transpose/12) * 2^CV`. `transpose` and `detune` are static offsets baked at note-on; `pitch_cv` is the per-block modulation term. This allows a second VCO instance to be tuned to a fixed interval (e.g. `transpose=-12` for sub-octave, `detune=-14` for ≈ 1/10 semitone chorus) independently of the first.
  - **Footage convention** (Roland oscillator range selector): The `footage` parameter maps the traditional pipe-organ footage labels to semitone transpose. Omitting `footage` (or setting `footage: 8`) leaves pitch at concert pitch. Values outside {2, 4, 8, 16, 32} are a no-op.

    | Footage | Semitones | Typical use |
    |---------|-----------|-------------|
    | 2'      | +24       | piccolo, very high leads |
    | 4'      | +12       | flute, high woodwinds |
    | 8'      | 0         | concert pitch (default) |
    | 16'     | −12       | cello, low brass |
    | 32'     | −24       | tuba, contrabass, sub-bass |

    `footage` and `transpose` write to the same internal offset. Set one or the other per patch, not both.
  - **VCO Hard Sync**: `sync_out` (PORT_AUDIO out) emits a 1.0 pulse on the sample where the internal sawtooth phase wraps. `sync_in` (PORT_AUDIO in) resets all oscillator phases on receipt of a positive pulse, locking the slave's period to the master. Connect `VCO_MASTER:sync_out → VCO_SLAVE:sync_in`. The master node must appear before the slave in the `chain` array so it is pulled first each block.
- **Architecture Note**: Two independent `COMPOSITE_GENERATOR` instances can coexist in the same voice chain with distinct tags (e.g. `"VCO1"`, `"VCO2"`). Each is tuned independently via its `pitch_cv` input, enabling interval stacking, detuning, and hard sync.

### LFO (Low Frequency Oscillator)
- **Type name**: `LFO`
- **Purpose**: Sub-audio modulation source.
- **Ports**:
  - `PORT_CONTROL` in `rate_cv` (unipolar)
  - `PORT_CONTROL` in `reset` (unipolar, lifecycle-style trigger)
  - `PORT_CONTROL` out `control_out` (bipolar)
  - `PORT_CONTROL` out `control_out_inv` (bipolar — inverted copy of `control_out`; always exactly −1 × `control_out`. Hardware: Roland M-150 OUT B. Eliminates the need for a separate `INVERTER` node when two destinations require counter-phase modulation, e.g. filter opens while VCA closes.)
- **Parameters**: `rate` (0.01–20 Hz — used when `sync=false`), `intensity` (0.0–1.0), `waveform` (enum: Sine=0, Triangle=1, Square=2, Saw=3; S&H planned), `delay` (0.0–10.0s, default 0.0 — time after note gate-on before modulation onset begins; output remains zero during the delay window then ramps to full depth), `sync` (bool, default `false` — when `true`, `rate` is ignored and LFO period is derived from engine tempo + `division`), `division` (enum index 0–10, default `2` (`"quarter"`) — beat subdivision when `sync=true`; see Transport Clock division table in ARCH_PLAN.md §Phase 27D)
- **Modulation Logic**: Routing LFO output to `pitch_cv` produces vibrato (FM); routing to VCA `gain_cv` produces tremolo (AM); routing to VCF `cutoff_cv` produces **growl** — a wavering of tone color at the LFO rate (Roland §5-5). The `delay` parameter implements the Roland DEL knob used on flute and string patches to produce built-in delayed vibrato without requiring a separate `CV_MIXER` + second `ADSR_ENVELOPE`. Route `control_out` → VCA `gain_cv` and `control_out_inv` → VCF `cutoff_cv` to produce counter-sweeping tremolo+filter modulation from a single LFO.
- **Tempo sync** (Phase 27D): When `sync=true`, LFO period = `(60 / bpm) × division_multiplier` beats. A `"whole"` LFO at 120 BPM completes one cycle every 2 seconds — useful for tempo-locked filter sweeps and tremolo. Rate changes from tempo automation glide smoothly.
- **Note**: Both outputs are `PORT_CONTROL`. Must not be patched directly into an audio mix.

### Noise Generator
- **Type name**: `WHITE_NOISE`
- **Purpose**: Stochastic signal generation.
- **Ports**:
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: `color` (enum: White=0, Pink=1; default White)
- **Modes**:
  - **White Noise**: Random combination of all frequencies. Power must increase by 3 dB per octave. Perceptually bright; suits wind attacks, hi-hats, and initial consonants on breath instruments.
  - **Pink Noise**: White noise passed through Paul Kellett's 7-pole IIR filter to equalize energy per octave (−3dB/octave). Perceptually flat; suits surf, wind body, and rain.
- **Usage note**: The noise oscillator is embedded in `COMPOSITE_GENERATOR` as waveform channel 6 (`noise_gain`). For standalone noise-only voices (e.g., percussion, wind) the `WHITE_NOISE` module may be instantiated directly as the first chain node, keeping `noise_gain` at 1.0 and all other gains at 0.0.

### Drawbar Organ
- **Type name**: `DRAWBAR_ORGAN`
- **Purpose**: Additive synthesis modelling a Hammond-style tonewheel organ. Nine sine oscillators at fixed harmonic ratios corresponding to drawbar footage (16', 8', 4', 2⅔', 2', 1⅗', 1½', 1⅓', 1').
- **Ports**:
  - `PORT_CONTROL` in `pitch_cv` (bipolar, 1V/oct)
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: `drawbar_16` (16'), `drawbar_513` (5⅓'), `drawbar_8` (8'), `drawbar_4` (4'), `drawbar_223` (2⅔'), `drawbar_2` (2'), `drawbar_135` (1⅗'), `drawbar_113` (1⅓'), `drawbar_1` (1') — all 0.0–8.0 (Hammond 9-step scale, not normalised 0–1). `percussion` (bool) and `percussion_decay` (0.0–2.0s) are **planned** (Phase 16+, deferred until percussion chiff feature lands)
- **Note**: Gate-through operation is achieved by setting ADSR attack=0, decay=0, sustain=1.0, release short. Verified by Bach organ tests.

---

## 2. Filters (Timbre Shaping)

All four filter types are **first-class chain nodes** — inserted into `signal_chain_` via `engine_add_module` exactly like VCO or VCA. No filter is a hardcoded `Voice` member. CV routing is fully live: `cutoff_cv` connections apply 1V/oct exponential modulation each block without altering the base cutoff anchor.

### Common filter ports (all four types)
- `PORT_AUDIO` in `audio_in`
- `PORT_AUDIO` out `audio_out`
- `PORT_CONTROL` in `cutoff_cv` (bipolar, 1V/oct — applies `actual = base_cutoff × 2^cv` each block)
- `PORT_CONTROL` in `res_cv` (unipolar — additive resonance boost)
- `PORT_CONTROL` in `kybd_cv` (bipolar, 1V/oct keyboard tracking)

### VCF — Moog Ladder
- **Type name**: `MOOG_FILTER`
- **Purpose**: 4-pole transistor ladder low-pass (−24 dB/oct). Smooth, creamy, thick — the gold standard for fat bass.
- **Additional ports**: `PORT_AUDIO` in `fm_in` (audio-rate cutoff FM)
- **Parameters**: `cutoff` (20–20000 Hz, log), `resonance` (0.0–1.0), `hpf_cutoff` (0–3 stepped HPF stage)
- **Character**: Full `tanh` saturation in feedback path and stage updates. Self-oscillates at `resonance ≈ 1.0`. Ringing transients at high resonance.

### VCF — TB-303 Diode Ladder
- **Type name**: `DIODE_FILTER`
- **Purpose**: TB-303 acid character — rubbery, squelchy, the "mistake" that launched a genre.
- **Additional ports**: `PORT_AUDIO` in `fm_in` (audio-rate cutoff FM)
- **Parameters**: `cutoff` (20–20000 Hz, log), `resonance` (0.0–1.0), `hpf_cutoff` (0–3 stepped HPF stage), `env_depth` (0.0–6.0, default 3.0 — scales how much a connected `cutoff_cv` envelope signal sweeps the cutoff; 0 = CV has no effect, 3 = one-octave sweep at full envelope, 6 = two-octave sweep)
- **Character**: 4-pole ladder (24 dB/oct) with `tanh` saturation in all stages and a built-in high-pass in the feedback path (~100 Hz) to keep self-oscillation from building DC. Resonance close to 1.0 produces the signature 303 squelch and self-oscillation sine.
- **Filter state**: Filter delay lines are **not reset on note_on**. State (including resonance buildup) persists across consecutive notes, replicating the 303's behaviour where the filter continues from its previous position at each new gate — essential for the characteristic acid squelch on rapid-fire riffs.
- **TB-303 two-envelope pattern**: On the real 303 the Decay knob controls **only the filter sweep** — the VCA follows the gate (open while key held, close on release) and is not affected by Decay at all. Replicate this with two separate envelopes: `AD_ENVELOPE` (variable decay) → `DIODE_FILTER:cutoff_cv`; `ADSR_ENVELOPE` (sustain=1.0, short release ≈20ms) → `VCA:gain_cv`. The ADSR acts as a shaped gate — fast attack, full sustain, quick release — so the VCA opens and closes cleanly with the note while the AD sweeps the filter independently.

### VCF — SH-101 CEM / IR3109
- **Type name**: `SH_FILTER`
- **Purpose**: Roland SH-101 character — clean, liquid, resonant. Stable self-oscillation.
- **Additional ports**: `PORT_AUDIO` in `fm_in` (audio-rate cutoff FM)
- **Parameters**: `cutoff` (20–20000 Hz, log), `resonance` (0.0–1.0)
- **Character**: 4-pole ladder (24 dB/oct) with algebraic soft-clip `x/(1+|x|)` in the feedback path (gentler than Moog `tanh`) and fully linear stage updates. More open high-end at moderate resonance; very stable self-oscillation sine tone at `resonance = 1.0`. Suits solid, punchy lead lines and bass patches where Moog weight would be excessive.

### VCF — Korg MS-20
- **Type name**: `MS20_FILTER`
- **Purpose**: Korg MS-20 character — aggressive, screaming, gritty. Two 2-pole (12 dB/oct) sections.
- **Additional ports**: `PORT_AUDIO` in `fm_in` (audio-rate cutoff FM)
- **Parameters**: `cutoff` (20–20000 Hz, log — LP section), `cutoff_hp` (20–2000 Hz, log — HP section, default 80 Hz), `resonance` (0.0–1.0), `fm_depth` (0.0–1.0, default 0.0)
- **Character**: Chamberlin SVF HP section (cutoff_hp) followed by SVF LP section (cutoff). Each section is 12 dB/oct (2-pole). The shallow slope means more harmonics bleed through than a 4-pole filter — contributing to the characteristic dirtiness. LP self-oscillates at `resonance ≈ 0.9`. `cutoff_hp` removes low-end mud and reinforces the perceived aggression.
- **Topology**: `input → HP (2-pole, cutoff_hp) → LP (2-pole, cutoff) → output`

### Band Pass Filter
- **Type name**: `BAND_PASS_FILTER`
- **Purpose**: Passes a band of frequencies between a low cutoff and a high cutoff, attenuating frequencies outside that band. Implemented internally as a 2-pole biquad (Audio EQ Cookbook). Useful for formant sculpting, telephone/radio effects, and mid-range emphasis.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` in `fm_in` (audio-rate cutoff FM)
  - `PORT_AUDIO` out `audio_out`
  - `PORT_CONTROL` in `cutoff_cv` (bipolar, 1V/oct)
  - `PORT_CONTROL` in `res_cv` (unipolar)
  - `PORT_CONTROL` in `kybd_cv` (bipolar, 1V/oct)
- **Parameters**: `cutoff` (20–20000 Hz, log), `resonance` (0.0–1.0), `fm_depth` (0.0–1.0, default 0.0)

### Band Reject Filter
- **Type name**: `BAND_REJECT_FILTER`
- **Purpose**: Attenuates a band of frequencies, passing everything outside that band. Equivalent to a LPF and HPF in parallel with their outputs summed (Roland §5-7, Fig 5-7c). Used for notch effects and comb filtering.
- **Status**: Planned. Requires parallel signal path architecture (see Known Gaps).

### Resonator
- **Type name**: `RESONATOR`
- **Purpose**: Emulation of physical acoustic body resonances/cavities.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out`
  - `PORT_CONTROL` in `freq_cv` (bipolar)
- **Parameters**: `frequency` (20–20000 Hz, log), `decay` (0.0–4.0s)
- **Status**: Planned.

### High Pass Filter
- **Type name**: `HIGH_PASS_FILTER`
- **Purpose**: Blocks low frequencies to brighten synthesized sounds. Implemented as a 2-pole biquad (Audio EQ Cookbook).
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` in `fm_in` (audio-rate cutoff FM)
  - `PORT_AUDIO` out `audio_out`
  - `PORT_CONTROL` in `cutoff_cv` (bipolar, 1V/oct)
  - `PORT_CONTROL` in `res_cv` (unipolar)
  - `PORT_CONTROL` in `kybd_cv` (bipolar, 1V/oct)
- **Parameters**: `cutoff` (20–20000 Hz, log), `resonance` (0.0–1.0), `fm_depth` (0.0–1.0, default 0.0)
- **Psychoacoustic Rule**: Even with heavy attenuation of the fundamental frequency, the engine should rely on the human ear's ability to "mentally" furnish the missing fundamental pitch as long as natural harmonic series overtones are present.

---

## 3. Amplitude & Dynamics

### VCA (Voltage Controlled Amplifier)
- **Type name**: `VCA`
- **Purpose**: Gain control and output stage. Distinct from the Envelope Generator.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out`
  - `PORT_CONTROL` in `gain_cv` (unipolar)
  - `PORT_CONTROL` in `initial_gain_cv` (unipolar) — wired as of Phase 26
- **Parameters**: `initial_gain` (0.0–1.0, default 1.0), `response_curve` (0.0–1.0, default 0.0 — implemented Phase 26)
- **Behaviour**:
  - **`gain_cv`**: Per-block gain envelope (from `ADSR_ENVELOPE` or `AD_ENVELOPE`). Applied as: `output[i] = audio[i] × effective_gain[i] × scale`.
  - **`response_curve`** (Phase 26): Blends between linear (0.0) and exponential (1.0) gain law. `effective_gain = lerp(g, g², response_curve)` where `g = gain_cv[i]`. Exponential is perceptually uniform for fades and percussive decays.
  - **`initial_gain`**: Static gain floor applied as `scale = initial_gain × base_amplitude_` when no `initial_gain_cv` is connected.
  - **`initial_gain_cv`** (Phase 26): When connected, overrides `initial_gain`: `scale = initial_gain_cv[0] × base_amplitude_`. Enables live CV control of the VCA floor for tremolo with offset (prevents VCA fully closing on LFO negative half-cycles).
  - When no `gain_cv` connection is present the VCA applies `scale` directly, ensuring output level is always defined.

### Envelope Generator (ADSR)
- **Type name**: `ADSR_ENVELOPE`
- **Purpose**: Transient shaping. Produces a control signal driven by gate events.
- **Ports**:
  - `PORT_CONTROL` in `gate_in` (unipolar — **lifecycle port**, driven by VoiceContext)
  - `PORT_CONTROL` in `trigger_in` (unipolar — **lifecycle port**)
  - `PORT_CONTROL` in `ext_gate_in` (unipolar — **non-lifecycle**; wirable via `connections_`; OR'd with `gate_in` at runtime; allows LFO square wave, `GATE_DELAY` output, or any other control source to re-trigger the envelope independently of note events)
  - `PORT_CONTROL` out `envelope_out` (unipolar)
- **Parameters**: `attack` (0.0–10.0s, log), `decay` (0.0–10.0s, log), `sustain` (0.0–1.0), `release` (0.0–10.0s, log)
- **Curve shape**: Exponential attack, decay, and release by default (perceptually natural).
- **Note**: Output is `PORT_CONTROL` only. To shape amplitude: patch `envelope_out` → VCA `gain_cv`. To shape filter: patch `envelope_out` → VCF `cutoff_cv`.
- **Percussion Trill usage**: Route LFO `control_out` → `ADSR_ENVELOPE` `ext_gate_in`. The LFO square wave triggers the envelope at its rate, producing rapid re-triggered decay shapes independent of key hold. This replicates the Roland System 100M Percussion Trill topology (Vol 2, Fig 3-13).

### Envelope Generator (AD)
- **Type name**: `AD_ENVELOPE`
- **Purpose**: Two-stage Attack-Decay transient generator. Output rises to peak during attack then falls to zero during decay — no sustain or release. Ideal for percussive sounds (bells, plucks, hits) where the envelope must complete its shape regardless of key hold duration.
- **Ports**:
  - `PORT_CONTROL` in `gate_in` (unipolar — **lifecycle port**, driven by VoiceContext)
  - `PORT_CONTROL` out `envelope_out` (unipolar)
- **Parameters**: `attack` (0.0–10.0s, log), `decay` (0.0–10.0s, log)
- **Note**: Output is `PORT_CONTROL` only. Patch `envelope_out` → VCA `gain_cv` for percussive amplitude shaping.

### Noise Gate
- **Type name**: `NOISE_GATE`
- **Purpose**: Suppresses audio signal when its level falls below a threshold, blocking noise during silent passages. When a signal above the threshold is detected, the gate opens quickly; when the signal drops below threshold, the gate closes slowly to follow the natural decay. Modelled on the Boss NF-1 Noise Gate (Roland Recording §4-5, Fig 4-4).
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: `threshold` (0.0–1.0 — level below which the gate closes; set just above the highest expected noise floor), `attack` (0.0–0.1s — time for gate to open fully when signal exceeds threshold; should be very fast to preserve note attacks), `decay` (0.0–2.0s — time for gate to close after signal drops below threshold; controls how naturally the tail fades; Roland calls this the DECAY knob on the NF-1)
- **Behavioral Logic**: Internally uses an envelope detector (peak or RMS) on the input signal. When the detected envelope rises above `threshold`, the gain ramps to 1.0 in `attack` time. When the detected envelope falls below `threshold`, the gain ramps to 0.0 in `decay` time. Output = `audio_in * gain`.
- **Usage**: In synthesis, a noise gate is useful for suppressing noise-floor bleed from oscillators between notes, or for cleaning up a `WHITE_NOISE` voice so it is silent between triggered events. As a global FX it removes tape-style hiss from sustained pauses.
### Envelope Follower
- **Type name**: `ENVELOPE_FOLLOWER`
- **Purpose**: Extracts a dynamic control signal from an audio input.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_CONTROL` out `envelope_out` (unipolar)
- **Parameters**: `attack` (0.0–1.0s), `release` (0.0–2.0s)

---

## 4. Modulation & CV Utilities

These modules operate entirely in the control domain.

### Maths / Function Generator
- **Type name**: `MATHS`
- **Purpose**: Envelope, slew limiter, or LFO with configurable curve shapes. Acts as portamento when patched between keyboard pitch CV and VCO `pitch_cv`.
- **Ports**:
  - `PORT_CONTROL` in `cv_in` (bipolar)
  - `PORT_CONTROL` out `cv_out` (bipolar)
- **Parameters**: `rise` (0.0–10.0s), `fall` (0.0–10.0s), `curve` (enum: Log, Linear, Exponential)

### CV Mixer / Attenuverter
- **Type name**: `CV_MIXER`
- **Purpose**: Combines, scales, and inverts control signals before routing to a destination. Negative mix weights implement inversion.
- **Ports**:
  - `PORT_CONTROL` in `cv_in_1` … `cv_in_4` (bipolar)
  - `PORT_CONTROL` out `cv_out` (bipolar)
  - `PORT_CONTROL` out `cv_out_inv` (bipolar — always −1 × `cv_out`; M-132 INV OUT precedent; Phase 27E addition; implemented via the `_inv`-suffix convention in `Voice::pull_mono` — no separate buffer allocated)
- **Parameters**: `gain_1` … `gain_4` (−1.0–1.0), `offset` (−1.0–1.0, default 0.0)
- **Bias / DC Offset**: The `offset` parameter injects a constant DC level into the output signal. This is the electronic equivalent of adding a bias voltage and is used to shift a bipolar signal (e.g. LFO output in [−1, 1]) into unipolar territory so a single VCA can produce tremolo without fully gating off on the negative LFO half-cycle. Example: `offset=0.5` + `gain_1=0.5` on a full-range bipolar LFO yields a unipolar tremolo depth in [0, 1].
- **Delayed Vibrato**: Route LFO `control_out` → `CV_MIXER` `cv_in_1`. Route a second `ADSR_ENVELOPE` `envelope_out` → `CV_MIXER` `cv_in_2` (acts as VCA for the LFO signal). The slow-attack envelope ramps the LFO gain from zero, producing vibrato that only appears after the note onset — the characteristic technique for bowed-string and woodwind patches. Connect `CV_MIXER` `cv_out` → `VCO` `pitch_cv`.
- **Counter-phase routing** (`cv_out_inv`): Connect `cv_out` → VCF `cutoff_cv` and `cv_out_inv` → VCA `gain_cv` so the filter opens as the VCA closes — without a separate `INVERTER` node.
- **M-132 gate-bias pattern** (Phase 27E): Mix `MIDI_CV.gate_cv` into `cv_in_2` (`gain_2=1.0`) alongside an LFO in `cv_in_1`, then set `offset=-0.5`. The gate pulse raises the net output above zero only while a key is held, so the LFO re-triggers `ADSR.ext_gate_in` only during key-held intervals — replicating the Roland M-132 −10V bias trick used in the Fig 3-4 banjo repeating-trigger patch. See BRIDGE_GUIDE.md §15.5 for the full patch JSON.
### CV Splitter
- **Type name**: `CV_SPLITTER`
- **Purpose**: Fans one control signal out to up to four destinations. Unity-gain fan-out is the default; unused outputs are ignored.
- **Ports**:
  - `PORT_CONTROL` in `cv_in` (bipolar)
  - `PORT_CONTROL` out `cv_out_1` … `cv_out_4` (bipolar)
- **Parameters**: `gain_1` … `gain_4` (−2.0–2.0, default 1.0 each)
- **Architecture note**: The Voice executor allocates **one output buffer per mod_source tag**. All four `cv_out` ports on a `CV_SPLITTER` instance read the same underlying buffer (scaled by `gain_1`). `gain_2` … `gain_4` are declared but not applied in the current executor — per-branch depth scaling must be achieved by inserting a `CV_SCALER` on each downstream branch.
- **Usage**: Patch one ADSR `envelope_out` → `CV_SPLITTER` `cv_in`, then connect `cv_out_1` → VCA `gain_cv` and `cv_out_2` → `CV_SCALER` `cv_in` → VCF `cutoff_cv`. Set `CV_SCALER` `scale` to control the filter modulation depth independently of the amplitude path (e.g. Roland MOD IN attenuverter knob).

### CV Scaler / Attenuverter
- **Type name**: `CV_SCALER`
- **Purpose**: Multiplies a control signal by a fixed scale factor and adds an optional offset. Implements the Roland-style "MOD IN" depth/attenuverter knob — placing it between a fan-out source and a destination sets per-branch modulation depth without affecting the other branches.
- **Ports**:
  - `PORT_CONTROL` in `cv_in` (bipolar)
  - `PORT_CONTROL` out `cv_out` (bipolar)
- **Parameters**: `scale` (−10.0–10.0, default 1.0), `offset` (−1.0–1.0, default 0.0)
- **Usage**:
  - `scale=0.5` on the VCF branch of a CV_SPLITTER gives 50% envelope modulation depth to the filter while the VCA branch sees 100%.
  - `scale=-1.0` is equivalent to `INVERTER` (sign flip).
  - `scale=0.9` replicates the Roland System 100M "MOD IN at 9" setting used on trumpet and trombone patches.
  - `offset` shifts the resting CV level — use to centre a bipolar sweep or add a DC bias.
- **Brass family pattern** (Roland Fig 1-6): `ADSR → CV_SPLITTER → [cv_out_1 → VCA:gain_cv], [cv_out_2 → CV_SCALER → VCF:cutoff_cv]`. The `CV_SCALER` `scale` parameter directly corresponds to the Roland "VCF MOD IN" depth knob.

### S&H (Sample & Hold)
- **Type name**: `SAMPLE_HOLD`
- **Purpose**: Stepped modulation — freezes an input CV at each clock trigger.
- **Ports**:
  - `PORT_CONTROL` in `cv_in` (bipolar)
  - `PORT_CONTROL` in `clock_in` (unipolar)
  - `PORT_CONTROL` out `cv_out` (bipolar)
- **Usage**: Patch a noise source (or LFO sawtooth) into `cv_in` and an LFO square wave into `clock_in`. The output steps to a new random value at each clock edge, producing random pitch patterns when connected to `VCO` `pitch_cv` or random filter-color sweeps when connected to VCF `cutoff_cv`. Classic for wind/surf textures and random arpeggio effects.

### Gate Delay / Pulse Shaper
- **Type name**: `GATE_DELAY`
- **Purpose**: Delays a gate event by a fixed time before passing it to a downstream module. Used to create multi-stage pitch effects (e.g. wolf-whistle: a second VCO gate fires delayed, producing a pitch that glides upward after note onset) and for tape-pulse reconstruction where gate edges must be offset in time.
- **Ports**:
  - `PORT_CONTROL` in `gate_in` (unipolar — **lifecycle port**, driven by VoiceContext)
  - `PORT_CONTROL` in `gate_in_b` (unipolar — **non-lifecycle**, wirable via `connections_`; OR'd with `gate_in` at runtime. Hardware: Roland M-172 INPUT B. Allows a second independent gate source — e.g. an LFO square wave or another `GATE_DELAY` output — to trigger the delay chain in addition to the voice lifecycle gate.)
  - `PORT_CONTROL` out `gate_out` (unipolar)
- **Parameters**:
  - `delay_time` (0.0–6.0s, default 0.0 — time from input gate rising edge to output gate rising edge; hardware M-172 range 0.3ms–6s)
  - `gate_time` (0.0–6.0s, default 0.0 — output pulse width; when 0.0 the output mirrors the input gate duration (i.e. `gate_out` stays high as long as `gate_in` is held); when >0 the output fires a fixed-length pulse of `gate_time` seconds regardless of how long the input is held. Hardware: Roland M-172 GATE TIME control.)
- **Behavioral Rule**: When either `gate_in` or `gate_in_b` transitions high, `gate_out` remains low until `delay_time` has elapsed. Then: if `gate_time == 0`, `gate_out` mirrors the remaining hold duration of the triggering gate; if `gate_time > 0`, `gate_out` fires a fixed pulse of `gate_time` seconds. If the gate closes before `delay_time` elapses, `gate_out` never fires. On all inputs going low, `gate_out` drops low immediately (unless a `gate_time` pulse is in progress, in which case the pulse completes).

### Inverter
- **Type name**: `INVERTER`
- **Purpose**: Inverts a control signal: `out = scale * in` where `scale` defaults to −1.0. Used to produce counter-sweeping modulation — when an ADSR drives amplitude up, an inverted copy drives filter cutoff down, creating the characteristic harpsichord or plucked-string timbral signature.
- **Ports**:
  - `PORT_CONTROL` in `cv_in` (bipolar)
  - `PORT_CONTROL` out `cv_out` (bipolar)
- **Parameters**: `scale` (−2.0–2.0, default −1.0; allows attenuation alongside inversion)
- **Harpsichord usage**: Route `ADSR_ENVELOPE` `envelope_out` → `INVERTER` `cv_in`. Route `INVERTER` `cv_out` → VCF `cutoff_cv`. Route `ADSR_ENVELOPE` `envelope_out` directly → VCA `gain_cv`. The amplitude envelope and the inverted filter sweep are driven by the same ADSR simultaneously: as the note attacks and sustains the VCA opens while the filter closes, then as the note releases the VCA closes and the filter opens — the classic harpsichord brightness-on-decay.
- **Gong / metallic usage**: Route ring-mod envelope output through an `INVERTER` into a second resonant filter's cutoff to produce pitch-descending metallic decay.

### MIDI / Keyboard CV Source
- **Type name**: `MIDI_CV`
- **Purpose**: Converts MIDI note-on/off events into patchable CV signals. Always the first module in the chain (tag `KBD` by convention). Replaces the legacy auto-injection of pitch and gate — all routing is now explicit.
- **Ports**:
  - `PORT_CONTROL` out `pitch_cv` — 1 V/oct; C4 (MIDI 60) = 0 V, +1 V per octave up. Connect to `COMPOSITE_GENERATOR:pitch_base_cv`.
  - `PORT_CONTROL` out `gate_cv` — 1.0 while key held, 0.0 released. Connect to `ADSR_ENVELOPE:gate_cv`.
  - `PORT_CONTROL` out `velocity_cv` — note-on velocity normalised to [0, 1]. Connect to `VCA:initial_gain_cv`.
  - `PORT_CONTROL` out `aftertouch_cv` — channel aftertouch normalised to [0, 1]. Optional; connect to filter cutoff or VCA for expressive control.
- **Parameters**: none.
- **Ordering rule**: `bake()` topologically sorts `mod_sources_` so `MIDI_CV` is always pulled before any CV consumer (e.g. `CV_SPLITTER`, `CV_MIXER`) that depends on it. Patch authors do not need to manually order entries.
- **Multi-VCO pitch fan**: Use a `CV_SPLITTER` between `KBD:pitch_cv` and each oscillator's `pitch_base_cv`. The splitter passes the absolute 1 V/oct value unchanged to all outputs.
- **Filter keyboard tracking**: Not automatic. Wire `KBD:pitch_cv → VCF:kybd_cv` explicitly on patches that require it.
- **Percussion patches**: Include `MIDI_CV` for gate control even when pitch is irrelevant (noise sources have no `pitch_base_cv`). Omit the pitch connection; keep `gate_cv → ENV:gate_cv`.

---

## 5. Effects & Spatial

### Phase Shifter
- **Type name**: `PHASE_SHIFTER`
- **Purpose**: All-pass filter chain that shifts the phase of frequency components, producing phasing/flanging effects when the swept signal is mixed with the dry path. Corresponds to the Roland System 100M Model 172 Phase Shifter module. Used in string and solo violin patches to add movement and spatial width.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out`
  - `PORT_CONTROL` in `rate_cv` (unipolar)
  - `PORT_CONTROL` in `depth_cv` (unipolar)
- **Parameters**: `rate` (0.01–10 Hz — LFO rate of the internal phase sweep), `depth` (0.0–1.0), `stages` (int: 2, 4, 6, or 8 all-pass stages; more stages = deeper, richer phasing), `feedback` (0.0–0.99 — resonance/feedback amount; higher values produce sharper notches), `mix` (0.0–1.0, dry/wet blend; default 0.5)
- **Behavioral Logic**: Internally consists of a chain of first-order all-pass filters whose centre frequencies are swept by a built-in LFO (or `rate_cv`). At `mix=0.5` the comb-filter interaction between phase-shifted and dry signal produces the characteristic phasing sweep. At `mix=1.0` (wet only) phasing is not audible as there is no reference phase to interfere with.
- **Status**: Planned. (Roland System 100M Model 172; Vol 1 §4-4, Fig 4-12 Solo Violin)

### Juno Chorus
- **Type name**: `JUNO_CHORUS`
- **Purpose**: BBD-style stereo chorus emulating the Roland Juno-60 chorus circuit.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: `mode` (enum: Off, I, II, I+II), `rate` (0.1–10 Hz), `depth` (0.0–1.0)
- **Note**: Architecturally a **global FX module** — one instance processes the summed voice output, not a per-voice chain node. Per-voice instantiation is a temporary workaround pending global FX chain support (Phase 17).

### Spatial Processor
- **Type name**: `SPATIAL`
- **Purpose**: Panning and stereo field positioning.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `left_out`
  - `PORT_AUDIO` out `right_out`
  - `PORT_CONTROL` in `pan_cv` (bipolar)
- **Parameters**: `pan` (−1.0–1.0)
- **Distance Logic**: Emulated apparent distance must be controlled by the ratio of direct loudness to reverberation. A sound with high reverberation and low direct volume will appear physically distant.

### Freeverb Reverb
- **Type name**: `REVERB_FREEVERB`
- **Purpose**: Schroeder/Freeverb reverb — 8 parallel comb filters + 4 series all-pass filters per channel. Warm, dense room sound.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: `room_size` (0.0–1.0 — scales comb delay lengths; snap: buffer geometry fixed at construction), `damping` (0.0–1.0 — HF absorption in comb feedback path), `width` (0.0–1.0 — stereo decorrelation), `wet` (0.0–1.0 — wet/dry blend)
- **Architecture Note**: **Global post-chain module only** — added via `engine_post_chain_push(h, "REVERB_FREEVERB")`. Not instantiated per-voice. Applied after all voices are summed and before HAL write.

### FDN Reverb
- **Type name**: `REVERB_FDN`
- **Purpose**: Jean-Marc Jot Feedback Delay Network reverb — 8 mutually-prime delay lines, Householder unitary feedback matrix, per-line 1-pole absorption filter with exact T60 (`g_i = 10^(−3 × d_i / (T60 × sr))`). Smoother modal density than Freeverb; suited for long tails (organ, strings, brass).
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: `decay` (T60 in seconds, 0.1–20.0), `room_size` (0.0–1.0 — scales delay line lengths; snap: buffer geometry fixed at construction), `damping` (0.0–1.0 — HF absorption), `width` (0.0–1.0 — stereo decorrelation), `wet` (0.0–1.0 — wet/dry blend)
- **Architecture Note**: **Global post-chain module only** — added via `engine_post_chain_push(h, "REVERB_FDN")`.

### Distortion / Overdrive
- **Type name**: `DISTORTION`
- **Purpose**: Guitar/pedal-style distortion that saturates the input signal through a waveshaper. Replicates the character of a distortion or overdrive pedal placed after the instrument output.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out`
- **Parameters**:
  - `drive` (1–40, default 8) — input gain into the clipping stage. At 1 the waveshaper input is nearly clean; at 20+ the signal saturates to a near-square wave.
  - `character` (0–1, default 0.3) — waveshaper blend. `0` = symmetric tanh (soft/tube, odd harmonics only). `1` = asymmetric: positive peaks clip at +0.5 (fast tanh × 4 × 0.5) while negative peaks clip at −1.0 (normal tanh), introducing ~6 dB asymmetry and even harmonics audible at any drive setting.
- **Signal path**: 4× linear interpolation upsampling → waveshaper → 2-stage cascaded 1-pole IIR anti-aliasing LP → decimate by 4.
- **Parameter smoothing**: `drive` and `character` ramp linearly over 10 ms (480 samples at 48 kHz) to suppress parameter-change zipper noise.
- **Placement note**: Insert post-VCA to replicate "synth output jack → distortion pedal". Inserting pre-VCA distorts before envelope shaping, which is less conventional. See `acid_reverb.json` for the canonical TB-303 pedal topology.
- **Bass content**: No internal HPF — the full signal including low fundamentals is distorted. Place `HIGH_PASS_FILTER` before `DISTORTION` if pre-emphasis (bass-cut before clip stage) is wanted.

### Phaser
- **Type name**: `PHASER`
- **Purpose**: 4- or 8-stage all-pass ladder with LFO-swept pole frequency. Classic swirling phase-cancellation effect.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: `rate` (0.01–10 Hz — internal LFO sweep rate, used when `sync=false`), `depth` (0.0–1.0 — sweep depth), `feedback` (0.0–0.99 — resonance; sharper notches at high values), `stages` (int: 4 or 8; snap), `base_freq` (20–2000 Hz — centre frequency of sweep), `wet` (0.0–1.0 — wet/dry blend), `sync` (bool, default `false` — when `true`, `rate` is ignored and sweep rate is derived from engine tempo + `division`), `division` (enum index 0–10, default `1` (`"half"`) — beat subdivision when `sync=true`; see Transport Clock division table in ARCH_PLAN.md §Phase 27D)
- **Tempo sync** (Phase 27D): When `sync=true`, one full sweep cycle = `(60 / bpm) × division_multiplier` seconds. A `"whole"` phaser at 120 BPM sweeps once every 2 seconds.
- **Architecture Note**: **Global post-chain module only** — added via `engine_post_chain_push(h, "PHASER")`.

### Echo / Delay
- **Type name**: `ECHO_DELAY`
- **Purpose**: Time-based repetition and echo, including BBD-style modulated delay for metallic shimmer effects (e.g. cymbal, bell sustain).
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out`
  - `PORT_CONTROL` in `time_cv` (unipolar)
  - `PORT_CONTROL` in `feedback_cv` (unipolar)
- **Parameters**:
  - `time` (0.0–5.0s — absolute delay time; ignored when `sync=true`)
  - `feedback` (0.0–0.99 — feedback gain; >0 produces cascading repeats that decay geometrically; 0.0 = single echo, 0.5 = ~3–4 audible repeats, 0.85 = long wash)
  - `mix` (0.0–1.0 — wet/dry blend; 0.0 = dry only, 1.0 = wet only, 0.4 = typical slapback)
  - `mod_rate` (0.1–20 Hz, default 0.0 — rate of the built-in LFO that sweeps delay time; 0.0 disables modulation)
  - `mod_intensity` (0.0–1.0, default 0.0 — depth of delay-time sweep; at 1.0 the delay oscillates ±50% of `time` at `mod_rate`, producing metallic shimmer — see Roland Cymbal patch, Vol 2 §3-5, Fig 3-16)
  - `sync` (bool, default `false` — when `true`, `time` is ignored and delay length = `(60 / bpm) × division_multiplier`)
  - `division` (enum index 0–10, default `2` (`"quarter"`) — beat subdivision when `sync=true`; see Transport Clock division table in ARCH_PLAN.md §Phase 27D)
- **Tempo sync** (Phase 27D): When `sync=true`, delay time tracks engine BPM in real time (SmoothedParam ramp to avoid click on tempo change). `"dotted_eighth"` (0.75×) at 120 BPM = 375 ms — the classic U2/Edge floating delay. `"quarter"` = one echo per beat. `"eighth"` = slapback.
- **Repeat count**: Controlled by `feedback`. With `sync=true` and `feedback=0.65`, repeats decay by ~65% each cycle — at `"quarter"` division and 120 BPM you hear ~4 distinct repeats before they fall below noise. Set `feedback=0.0` for a clean single echo.
- **Note**: Internal circular delay buffer handles self-feedback — no graph-level feedback connection needed.
- **BBD Cymbal usage** (`sync=false`): Set `time` ≈ 20–40 ms, `feedback` ≈ 0.5–0.7, `mod_rate` ≈ 5–8 Hz, `mod_intensity` ≈ 0.3–0.6. The wobbling delay time produces the characteristic metallic shimmer of a struck cymbal or gong. Chain: `WHITE_NOISE` → `MOOG_FILTER` → `ECHO_DELAY` → percussive `VCA`.

---

## 6. Global I/O

### ~~Source Mixer~~ (Retired)
- **Type name**: `SOURCE_MIXER`
- **Status**: **Retired — not registered, cannot be used via `engine_add_module`.** Do not reference in patch files or code.
- Multi-waveform blending within a single VCO is handled by `COMPOSITE_GENERATOR`'s internal waveform mixer (controlled via `saw_gain`, `pulse_gain`, `sine_gain`, `sub_gain`, `triangle_gain`, `noise_gain` parameters). Cross-VCO summing of independent oscillator nodes uses `AUDIO_MIXER` (registered, wirable via port connections).

### Audio Mixer
- **Type name**: `AUDIO_MIXER`
- **Purpose**: Sums up to 4 audio signals into a single output with per-input gain control. The registered replacement for the retired `SOURCE_MIXER`. Use for dual- or multi-VCO additive synthesis, layering independent oscillator paths before a shared filter, or combining a main signal with a ring-modulated or noise component.
- **Ports**:
  - `PORT_AUDIO` in `audio_in_1` … `audio_in_4`
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: `gain_1` … `gain_4` (0.0–1.0, default 1.0 each — SmoothedParam, 10 ms ramp)
- **Behavioural notes**:
  - Only connected inputs contribute to the sum — unconnected inputs are zero.
  - Output is hard-clipped to ±1.0 after summing. Set `gain_N = 0.5` on each active input when mixing two equal-level sources to prevent saturation.
  - All gain changes ramp linearly over ~10 ms (SmoothedParam) to suppress zipper noise.
- **Usage**: Wire `VCO1.audio_out → AUDIO_MIXER.audio_in_1`, `VCO2.audio_out → AUDIO_MIXER.audio_in_2`, then `AUDIO_MIXER.audio_out → VCF.audio_in`. See `patches/group_strings.json` and `patches/banjo.json` for real examples.

### Audio Splitter
- **Type name**: `AUDIO_SPLITTER`
- **Purpose**: Fans one audio signal out to up to four destinations, each with independent gain scaling. Exact counterpart to `CV_SPLITTER` for the `PORT_AUDIO` domain. Required wherever a single audio source must feed multiple destinations — for example, when a VCO must simultaneously drive a `RING_MOD` input AND the main signal chain, or when the same oscillator feeds two parallel VCF branches.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out_1` … `audio_out_4`
- **Parameters**: `gain_1` … `gain_4` (0.0–2.0, default 1.0 each)
- **Usage**: Patch a VCO `audio_out` → `AUDIO_SPLITTER` `audio_in`, then connect `audio_out_1` → main chain (filter/VCA path) and `audio_out_2` → `RING_MOD` `audio_in_b` to use the same oscillator as both signal and FM modulator source. Also required for parallel-VCF topologies (two independent filters receiving the same oscillator).

### Ring Modulator
- **Type name**: `RING_MOD`
- **Purpose**: Non-linear frequency interaction between two audio signals, producing metallic and bell-like timbres inharmonic to the source waveforms.
- **Ports**:
  - `PORT_AUDIO` in `audio_in_a`
  - `PORT_AUDIO` in `audio_in_b`
  - `PORT_AUDIO` out `audio_out`
  - `PORT_CONTROL` in `mod_in` (bipolar — sub-audio modulation input; when connected, the LFO or slow control signal is multiplied with `audio_in_a` directly, enabling bowed tremolo and LFO-driven ring effects without requiring a second audio-rate VCO on `audio_in_b`; at LFO rates this behaves as an AM tremolo; at audio rates it behaves as classic ring modulation)
- **Parameters**: `mix` (0.0–1.0, dry/wet blend; default 1.0 = fully wet)
- **Behavioral Logic**:
  - Implements 4-quadrant multiplication: `out[n] = A[n] * B[n]`. This suppresses the carrier (both input frequencies) and outputs only the **sum** (A+B) and **difference** (A−B) sidebands.
  - When input A is a pitched VCO (e.g. 440 Hz) and input B is a second VCO at an inharmonic interval (e.g. 554 Hz), the output contains 994 Hz and 114 Hz — neither matching the input pitches — producing the characteristic metallic, bell, or gong quality.
  - A single VCO into both inputs reduces to squaring: `sin²(ωt) = ½ − ½·cos(2ωt)`, yielding a DC offset plus a doubled-frequency component. For perceptual use both inputs must be distinct signals.
  - **Bell patch**: VCO1 (sine, fundamental) → `audio_in_a`; VCO2 (sine, non-integer ratio e.g. 2.756×) → `audio_in_b`; RING_MOD output → percussive VCA envelope. See `patches/bell.json`.
  - **Choral/vocal formant patch**: one VCO + one VCO tuned to a voice-tract formant frequency → RING_MOD → VCA.

### Audio Output
- **Type name**: `AUDIO_OUTPUT`
- **Purpose**: Transparent chain-terminator sink. Provides an explicit, named output endpoint for patch editors that require one. Role: **SINK**.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
- **Parameters**: none
- **Behavioural notes**:
  - Inline audio from the preceding chain node passes through unchanged to the engine's summing bus.
  - Engine behaviour is identical whether or not `AUDIO_OUTPUT` is present — it is additive, not required.
  - `bake()` SINK exception: a SINK node (audio_in only, no audio_out) is permitted as the last chain node. The "last node must output PORT_AUDIO" check is bypassed for SINK-role modules.
- **Usage**: Add as the last chain node in patch JSON when your patch editor or UI requires an explicit output endpoint.

### Audio Input
- **Type name**: `AUDIO_INPUT`
- **Purpose**: Live audio source from a hardware line input or microphone (e.g. guitar into an effects chain, side-chain vocoder input). Role: **SOURCE**.
- **Ports**:
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: `device_index` (int, 0–16, default 0), `gain` (0.0–4.0, default 1.0)
- **Note**: Requires the audio driver to open a capture PCM path alongside the playback path. CoreAudio supports full-duplex natively; ALSA requires a separate `snd_pcm_open` in `SND_PCM_STREAM_CAPTURE`. Dispatched to the engine via the same block-pull mechanism as oscillators.
- **Current status**: Currently outputs silence. Live HAL routing deferred to Phase 25. The module is registered and accepted by `bake()` as a SOURCE at the chain head.
- **C API**: Call `engine_open_audio_input(handle, device_index)` to associate the node with a hardware capture device.
- **Usage**: Add as the first chain node.

### Audio File Reader
- **Type name**: `AUDIO_FILE_READER`
- **Purpose**: WAV/AIFF file playback source. Loads a file into memory and outputs it block by block, optionally looping. Role: **SOURCE**.
- **Ports**:
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: `loop` (0/1, default 0), `gain` (0.0–4.0, default 1.0)
- **String parameters**: `path` (file path to WAV or AIFF file) — set via `engine_set_tag_string_param(handle, tag, "path", "/path/to/file.wav")` after `bake()`.
- **Behavioural notes**:
  - File is loaded into memory at engine start. Sample-rate conversion via libsamplerate is applied off the audio thread when the file's sample rate differs from the engine's rate.
  - Underrun (end of file when `loop=0`) produces silence.
  - Supported formats: WAV (PCM 16/24/32, float 32/64) and AIFF (PCM 16/24/32, float 32). Mono and stereo files only. No compressed formats (MP3, AAC, OGG).
- **Dependencies**: requires libsndfile 1.2.2 and libsamplerate 0.2.2 (both pulled via CMake FetchContent).
- **Usage**: Add as the first chain node. Set the file path after `bake()`:
  ```c
  engine_set_tag_string_param(handle, "FILE_IN", "path", "/samples/loop.wav");
  ```

### Audio File Writer
- **Type name**: `AUDIO_FILE_WRITER`
- **Purpose**: Real-time WAV recorder. Captures the inline audio arriving at `audio_in` and writes it to a WAV file on disk. Role: **SINK**.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
- **Parameters**: `max_seconds` (0–86400, default 0 = unlimited), `max_file_mb` (0–4096, default 0 = unlimited)
- **String parameters**: `path` (output WAV file path) — set via `engine_set_tag_string_param(handle, tag, "path", "/path/to/output.wav")` after `bake()`.
- **Behavioural notes**:
  - The output file is opened on `engine_start` and flushed (WAV header finalised) on `engine_destroy`.
  - Call `engine_file_writer_flush(handle)` for an explicit intermediate sync without stopping the engine.
  - Recording stops automatically when `max_seconds` or `max_file_mb` is reached (if non-zero).
  - `bake()` SINK exception applies — this node is permitted as the last chain entry.
- **Dependencies**: requires libsndfile 1.2.2 (pulled via CMake FetchContent).
- **Usage**: Add as the last chain node. Set the file path after `bake()`:
  ```c
  engine_set_tag_string_param(handle, "FILE_OUT", "path", "/tmp/capture.wav");
  ```

---

## Implementation Rules

- **Port type enforcement**: `bake()` must reject any connection where a `PORT_AUDIO` output is connected to a `PORT_CONTROL` input, or vice versa.
- **Generator-first rule**: `bake()` must verify that the first node in `signal_chain_` outputs `PORT_AUDIO`.
- **VCA/Envelope separation**: `AdsrEnvelopeProcessor` produces `PORT_CONTROL` output only. A separate `VcaProcessor` node performs audio multiplication via its `gain_cv` input. They must not be merged.
- **Lifecycle ports**: `gate_in` and `trigger_in` are injected by `VoiceContext`, not wired via `connections_`. `bake()` skips connection validation for these port names.
- **Feedback connections**: Graph-level cross-node feedback (`"feedback": true` in connections) is parsed but **not executed** by the graph executor. Per-processor internal feedback (e.g. `ECHO_DELAY` internal delay line) is supported.
- **Multiple inputs**: A node with multiple input ports (Ring Modulator, CV Mixer) has all inputs gathered before `pull()` is called. The executor resolves all inputs before executing any node via `inject_audio()` / `inject_cv()` in `Voice::pull_mono` (implemented Phase 18).
- **Dynamic routing**: Modules are not hardcoded in `Voice`. `Voice` manages two lists — `signal_chain_` (PORT_AUDIO nodes) and `mod_sources_` (PORT_CONTROL generators) — with instance tags (e.g. `"VCO"`, `"ENV"`, `"LFO1"`) for parameter targeting and port connection. `add_processor()` routes each node automatically based on its output port type.
- **Global vs per-voice**: Per-voice modules live in `signal_chain_` or `mod_sources_`. Global modules (chorus, reverb, master bus) live in a separate global FX chain applied after voice summing. Do not instantiate global modules per-voice.
- **Mono-to-stereo paths**: The engine is mono-until-SummingBus. Two explicit paths introduce stereo before the bus: (1) Stereo FX processors (`JUNO_CHORUS`, `REVERB_FDN`, `REVERB_FREEVERB`) in the global post-chain — place via `engine_post_chain_push`; they receive summed mono and output stereo. (2) `AUDIO_SPLITTER` explicit copy — connect one mono source to `audio_in`, then `audio_out_1` → left path and `audio_out_2` → right path feeding a spatial/stereo processor downstream. Direct stereo within per-voice chains is not supported in the current architecture.
- **Role classification** (Phase 27C): Every registered module has an inferred `role` (`SOURCE`, `SINK`, or `PROCESSOR`) exposed via the JSON introspection API. `SOURCE` = PORT_AUDIO out, no PORT_AUDIO in. `SINK` = PORT_AUDIO in, no PORT_AUDIO out. `PROCESSOR` = everything else, including pure CV modules (`LFO`, `ADSR_ENVELOPE`, `CV_MIXER`, `MATHS`, `INVERTER`, etc.) which have no PORT_AUDIO ports at all, and `COMPOSITE_GENERATOR` (has PORT_AUDIO `fm_in`). UI tools should use `role` to filter modules when building a chain (e.g. only offer SOURCEs at the chain head, only offer SINKs or PROCESSORs as subsequent nodes).
- **Tempo-sync parameters**: `ECHO_DELAY`, `LFO`, and `PHASER` support `sync` (bool) + `division` (float index 0–10) parameters. When `sync=true` the processor's time/rate is derived from the engine's transport clock (`engine_set_tempo`, or automatically from SMF FF 51 tempo events). Division vocabulary is beat-relative and works in any time signature. Transport state is propagated zero-copy via `BlockContext` (a concrete `VoiceContext` carrying `bpm` + `beats_per_bar`) into every `do_pull` call each block. See ARCH_PLAN.md §Phase 27D for the full division table.
- **Sample rate**: All internal timing (ADSR curves, LFO rates, delay times) must derive from the runtime `sample_rate_` passed at construction. No hardcoded sample rate assumptions. Supported rates: 44100 Hz and 48000 Hz. If hardware reports a rate above 48000, the engine negotiates down to 48000.
- **Filter chain placement**: All four filter types (`MOOG_FILTER`, `DIODE_FILTER`, `SH_FILTER`, `MS20_FILTER`) are first-class chain nodes. Add them via `engine_add_module("MOOG_FILTER", "VCF")` and wire audio with `engine_connect_ports`. `Voice` no longer contains an internal `filter_` member — the hardcoded fallback path has been removed. Minimum filter chain: `COMPOSITE_GENERATOR → MOOG_FILTER → ADSR_ENVELOPE → VCA`.
- **Filter state persistence**: All `VcfBase` subclasses (`MOOG_FILTER`, `DIODE_FILTER`, `SH_FILTER`, `MS20_FILTER`) override `reset_on_note_on()` to return `false`. Filter delay lines and resonance buildup are **preserved across consecutive notes**. Only envelope generators and other non-filter chain nodes reset on `note_on`. This is essential for acid/TB-303 style patches where filter self-oscillation builds across a rapid-fire note sequence.
- **Per-tag parameter addressing**: Parameters are addressed by tag in the patch JSON using a nested object per tag (see PATCH_SPEC.md §Parameters Object). At runtime, `set_named_parameter` must be extended to accept an optional tag scope so that two nodes of the same type (e.g. `"VCO1"` and `"VCO2"`) can be addressed independently. The patch loader maps `parameters["VCO2"]["detune"]` to `voice.find_by_tag("VCO2")->apply_parameter("detune", value)`. Full centralised ramping is Phase 21 (ParameterManager); tag-scoped direct addressing should land with multi-oscillator support.
- **Parallel signal paths**: Not yet supported. The current `Voice` has a single `signal_chain_`. Multi-waveform mixing is handled by `COMPOSITE_GENERATOR` internally. Independent per-oscillator filter chains are a future architecture goal.

---

## Known Gaps vs. Roland System 100M Techniques

The following capabilities are demonstrated in Roland's *Practical Synthesis* documentation and are either absent or incompletely specified in this module set. Each item references the technique and documents the current gap.

| Technique | Source | Gap | Resolution |
|-----------|--------|-----|------------|
| Hard VCO sync | Clarinet, lead synth patches | ~~RESOLVED~~ `sync_out` (master trigger) and `sync_in` (slave phase-reset) ports added to `COMPOSITE_GENERATOR`; `Voice::pull_mono` injects sync buffer via `get_secondary_output("sync_out")`. Clarinet patch uses two-VCO topology: `VCO2:sync_out → VCO1:sync_in` | — |
| Delayed vibrato | Violin, flute, bowed strings | ~~RESOLVED~~ `CV_MIXER` (Phase 17) + second `ADSR_ENVELOPE` + `SmoothedParam` (Phase 21) parameter addressing all implemented | — |
| Portamento / glide | Lead and bass patches | `MATHS` (Phase 17) implemented as slew limiter; glide via existing `oscillator_set_frequency_glide`; pitch-dip patch uses `INVERTER` (Phase 17) | — |
| Keyboard tracking of VCF cutoff | Nearly all tonal patches | ~~RESOLVED~~ `MIDI_CV.pitch_cv → VCF.kybd_cv` explicit connection (Phase 27E); the `kybd_cv_nodes_` auto-injection cache was removed from `Voice` | — |
| Pink noise | Wind, surf, rain patches | ~~RESOLVED~~ `WHITE_NOISE` `color=1` implemented via Paul Kellett 7-pole IIR in Phase 18 | — |
| Gate delay (wolf-whistle) | Specialty pitch effects | ~~RESOLVED~~ `GATE_DELAY` implemented Phase 17 | — |
| Ring modulator | Bell, metallic, choral patches | ~~RESOLVED~~ `RING_MOD` implemented Phase 18; multi-input execution via `inject_audio()` in `Voice::pull_mono` | — |
| Filter as chain node | Pizzicato strings, two-VCF percussion, VCF self-oscillation, separate filter type per patch | ~~RESOLVED~~ All four filter types (`MOOG_FILTER`, `DIODE_FILTER`, `SH_FILTER`, `MS20_FILTER`) are now first-class chain nodes; `cutoff_cv` CV is routed via `apply_parameter` in `pull_mono`; `Voice::filter_` removed | — |
| Audio-rate filter FM | Tom Tom, Cow Bell, Bongo Drums patches | ~~RESOLVED~~ `fm_in` PORT_AUDIO input + `fm_depth` parameter added to all six filter types (`MOOG_FILTER`, `DIODE_FILTER`, `SH_FILTER`, `MS20_FILTER`, `HIGH_PASS_FILTER`, `BAND_PASS_FILTER`); summed into cutoff at audio rate | — |
| LFO as envelope gate source | Percussion Trill (Vol 2, Fig 3-13) | ~~RESOLVED~~ `ext_gate_in` non-lifecycle `PORT_CONTROL` input added to `ADSR_ENVELOPE` (Phase 17); OR'd with lifecycle `gate_in` | — |
| Two VCAs in series | Percussion amplitude shaping (linear dynamics + exponential decay) | No architecture gap — chain model supports sequential `VCA` nodes with different `response_curve` values | No new module needed |
| Band Reject Filter topology | Notch/comb effects (Roland §5-7, Fig 5-7c) | Requires LPF and HPF in **parallel** with outputs summed; current `Voice` has a single serial `signal_chain_` — parallel branches not yet supported | Extend `Voice` architecture to support forked/parallel signal paths; `BAND_REJECT_FILTER` specified above; deferred |
| VCO audio-rate pitch FM | §2-2, Fig 2-5 (two-VCO FM patch) | ~~RESOLVED~~ `fm_in` PORT_AUDIO port + `fm_depth` added to `COMPOSITE_GENERATOR` (Phase 18) | — |
| Ring mod LFO modulation | Vol 1 §2-5, Fig 2-19 (Violin Bowed Tremolo) | ~~RESOLVED~~ `mod_in` PORT_CONTROL input added to `RING_MOD` (Phase 18); control signal multiplies `audio_in_a` directly | — |
