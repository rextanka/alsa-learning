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

> **Current status**: Feedback execution is **not yet implemented**. The `"feedback": true` flag is parsed and stored but the graph executor does not maintain a previous-block output buffer. Feedback routing will be activated when `ECHO_DELAY` lands (Phase 17).

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
    std::string name;       // "cutoff", "attack_time"
    std::string label;      // human-readable: "Cutoff Frequency"
    float min, max, def;    // range and default
    bool  logarithmic;      // true for frequency/time params
};
```

Each processor calls `declare_parameter()` in its constructor alongside `declare_port()`.

---

## Module Registry

At library load time each processor `.cpp` registers its module type with `ModuleRegistry::instance()` via a static initializer:

```cpp
static bool reg = ModuleRegistry::instance().register_module({
    "MOOG_FILTER",
    "4-pole Moog ladder low-pass filter",
    ports,       // from declare_port() calls
    parameters,  // from declare_parameter() calls
    [](int sr) { return std::make_unique<MoogLadderProcessor>(sr); }
});
```

The registry is queryable via the C API (`engine_get_module_count`, `engine_get_module_type`, `engine_get_module_port`, `engine_get_module_parameter`). See BRIDGE_GUIDE.md §5.

---

## 1. Generators (Oscillators & Noise)

### VCO (Voltage Controlled Oscillator)
- **Type name**: `COMPOSITE_GENERATOR`
- **Purpose**: Primary periodic harmonic generation. Owns all waveform oscillators and a SourceMixer.
- **Ports**:
  - `PORT_CONTROL` in `pitch_cv` (bipolar, 1V/oct)
  - `PORT_CONTROL` in `pwm_cv` (bipolar)
  - `PORT_AUDIO` in `fm_in` (audio-rate frequency modulation; a VCO `audio_out` patched here is multiplied by `fm_depth` and summed into the pitch CV path, enabling VCO-to-VCO FM synthesis and extreme pitch sweeps not achievable at control rate)
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: `saw_gain`, `pulse_gain`, `sine_gain`, `triangle_gain`, `sub_gain`, `wavetable_gain`, `noise_gain` (all 0.0–1.0), `pulse_width` (0.0–0.5), `wavetable_type` (int enum), `transpose` (integer semitones, −24–+24, default 0), `detune` (float cents, −100–+100, default 0.0), `fm_depth` (0.0–1.0, default 0.0 — scales the `fm_in` signal before summing with `pitch_cv`)
- **Logic**:
  - **Exponential Pitch**: Frequency response must follow an exponential curve (f = f₀ · 2^CV) corresponding to the standard 1V/octave tracking.
  - **Harmonic Spectra Rules**:
    - **Sawtooth**: Must contain all harmonics (even and odd). Amplitude decreases at a rate of 1/n (where n is the harmonic number). Waveform inversion (ramp-up vs. ramp-down) is permitted as it does not alter perceived tone color.
    - **Square**: Must contain only odd-numbered harmonics. Amplitude decreases at a rate of 1/n.
    - **Triangle**: Must contain only odd-numbered harmonics, with amplitudes decreasing sharply at a rate of 1/n².
  - All waveforms produced simultaneously, blended by internal SourceMixer. Sub-oscillator phase-coupled to pulse oscillator.
  - **Pitch offset**: The final frequency is computed as `f = f_base * 2^(detune/1200) * 2^(transpose/12) * 2^CV`. `transpose` and `detune` are static offsets baked at note-on; `pitch_cv` is the per-block modulation term. This allows a second VCO instance to be tuned to a fixed interval (e.g. `transpose=-12` for sub-octave, `detune=-14` for ≈ 1/10 semitone chorus) independently of the first.
  - **VCO Hard Sync** (planned): When a `sync_in` pulse is received, the oscillator's phase is reset to zero. This forces the slave waveform to restart at the master's period, emphasizing odd harmonics that align with the master. Used in clarinet and aggressive lead patches. The `sync_in` port is not yet declared; pending a multi-VCO chain topology where one COMPOSITE_GENERATOR drives another's sync input.
- **Architecture Note**: Two independent `COMPOSITE_GENERATOR` instances can coexist in the same voice chain with distinct tags (e.g. `"VCO1"`, `"VCO2"`). Each is tuned independently via its `pitch_cv` input, enabling interval stacking, detuning, and (eventually) hard sync.

### LFO (Low Frequency Oscillator)
- **Type name**: `LFO`
- **Purpose**: Sub-audio modulation source.
- **Ports**:
  - `PORT_CONTROL` in `rate_cv` (unipolar)
  - `PORT_CONTROL` in `reset` (unipolar, lifecycle-style trigger)
  - `PORT_CONTROL` out `control_out` (bipolar)
- **Parameters**: `rate` (0.01–20 Hz), `intensity` (0.0–1.0), `waveform` (enum: Sine=0, Triangle=1, Square=2, Saw=3; S&H planned), `delay` (0.0–10.0s, default 0.0 — time after note gate-on before modulation onset begins; output remains zero during the delay window then ramps to full depth)
- **Modulation Logic**: Routing LFO output to `pitch_cv` produces vibrato (FM); routing to VCA `gain_cv` produces tremolo (AM); routing to VCF `cutoff_cv` produces **growl** — a wavering of tone color at the LFO rate (Roland §5-5). The `delay` parameter implements the Roland DEL knob used on flute and string patches to produce built-in delayed vibrato without requiring a separate `CV_MIXER` + second `ADSR_ENVELOPE`.
- **Note**: Output is `PORT_CONTROL`. Must not be patched directly into an audio mix.

### Noise Generator
- **Type name**: `WHITE_NOISE`
- **Purpose**: Stochastic signal generation.
- **Ports**:
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: `color` (enum: White=0, Pink=1; default White)
- **Modes**:
  - **White Noise**: Random combination of all frequencies. Power must increase by 3 dB per octave. Perceptually bright; suits wind attacks, hi-hats, and initial consonants on breath instruments.
  - **Pink Noise** (planned): White noise passed through a −3dB/octave shelving filter to equalize energy per octave. Perceptually flat; suits surf, wind body, and rain. Implement via a chain of 3–5 first-order allpass/IIR shelves approximating the −10dB/decade slope.
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
- **Parameters**: `cutoff` (20–20000 Hz, log), `resonance` (0.0–1.0), `hpf_cutoff` (0–3 stepped HPF stage)
- **Character**: 3/4-pole blend — at full resonance the 3-pole (stage[2]) output dominates, giving an effective ~18 dB/oct rolloff. Per-stage `tanh` saturation adds the characteristic harmonic distortion. The blend shifts continuously from ~4-pole at zero resonance to ~3-pole at maximum, matching the 303's measured behaviour.

### VCF — SH-101 CEM / IR3109
- **Type name**: `SH_FILTER`
- **Purpose**: Roland SH-101 character — clean, liquid, resonant. Stable self-oscillation.
- **Additional ports**: `PORT_AUDIO` in `fm_in` (audio-rate cutoff FM)
- **Parameters**: `cutoff` (20–20000 Hz, log), `resonance` (0.0–1.0)
- **Character**: 4-pole ladder (24 dB/oct) with algebraic soft-clip `x/(1+|x|)` in the feedback path (gentler than Moog `tanh`) and fully linear stage updates. More open high-end at moderate resonance; very stable self-oscillation sine tone at `resonance = 1.0`. Suits solid, punchy lead lines and bass patches where Moog weight would be excessive.

### VCF — Korg MS-20
- **Type name**: `MS20_FILTER`
- **Purpose**: Korg MS-20 character — aggressive, screaming, gritty. Two 2-pole (12 dB/oct) sections.
- **Parameters**: `cutoff` (20–20000 Hz, log — LP section), `cutoff_hp` (20–2000 Hz, log — HP section, default 80 Hz), `resonance` (0.0–1.0)
- **Character**: Chamberlin SVF HP section (cutoff_hp) followed by SVF LP section (cutoff). Each section is 12 dB/oct (2-pole). The shallow slope means more harmonics bleed through than a 4-pole filter — contributing to the characteristic dirtiness. LP self-oscillates at `resonance ≈ 0.9`. `cutoff_hp` removes low-end mud and reinforces the perceived aggression.
- **Topology**: `input → HP (2-pole, cutoff_hp) → LP (2-pole, cutoff) → output`

### Band Pass Filter
- **Type name**: `BAND_PASS_FILTER`
- **Purpose**: Passes a band of frequencies between a low cutoff and a high cutoff, attenuating frequencies outside that band. Implemented internally as a LPF and HPF in series (Roland §5-6, Fig 5-6c). Useful for formant sculpting, telephone/radio effects, and mid-range emphasis.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out`
  - `PORT_CONTROL` in `cutoff_low_cv` (bipolar)
  - `PORT_CONTROL` in `cutoff_high_cv` (bipolar)
- **Parameters**: `cutoff_low` (20–20000 Hz, log — lower edge of pass band), `cutoff_high` (20–20000 Hz, log — upper edge of pass band), `resonance` (0.0–1.0)
- **Status**: Planned.

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

### High Pass Filter (Global/Static)
- **Type name**: `HIGH_PASS_FILTER`
- **Purpose**: Blocks low frequencies to brighten synthesized sounds.
- **Psychoacoustic Rule**: Even with heavy attenuation of the fundamental frequency, the engine should rely on the human ear's ability to "mentally" furnish the missing fundamental pitch as long as natural harmonic series overtones are present.
- **Status**: Planned.

---

## 3. Amplitude & Dynamics

### VCA (Voltage Controlled Amplifier)
- **Type name**: `VCA`
- **Purpose**: Gain control and output stage. Distinct from the Envelope Generator.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out`
  - `PORT_CONTROL` in `gain_cv` (unipolar)
  - `PORT_CONTROL` in `initial_gain_cv` (unipolar)
- **Parameters**: `initial_gain` (0.0–1.0), `response_curve` (enum: Linear, Exponential)
- **Behaviour**:
  - **Linear Mode**: Output level is directly proportional to control voltage input.
  - **Exponential Mode**: Output level follows an exponential curve relative to input CV. Critical for producing natural-sounding percussive decays.
  - `initial_gain_cv` sets a resting DC level so a bipolar LFO can produce tremolo without fully closing the VCA on negative half-cycles.
  - When no `gain_cv` connection is present the VCA applies `base_amplitude_` at unity gain, ensuring output level is always defined.

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

### Noise Gate
- **Type name**: `NOISE_GATE`
- **Purpose**: Suppresses audio signal when its level falls below a threshold, blocking noise during silent passages. When a signal above the threshold is detected, the gate opens quickly; when the signal drops below threshold, the gate closes slowly to follow the natural decay. Modelled on the Boss NF-1 Noise Gate (Roland Recording §4-5, Fig 4-4).
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: `threshold` (0.0–1.0 — level below which the gate closes; set just above the highest expected noise floor), `attack` (0.0–0.1s — time for gate to open fully when signal exceeds threshold; should be very fast to preserve note attacks), `decay` (0.0–2.0s — time for gate to close after signal drops below threshold; controls how naturally the tail fades; Roland calls this the DECAY knob on the NF-1)
- **Behavioral Logic**: Internally uses an envelope detector (peak or RMS) on the input signal. When the detected envelope rises above `threshold`, the gain ramps to 1.0 in `attack` time. When the detected envelope falls below `threshold`, the gain ramps to 0.0 in `decay` time. Output = `audio_in * gain`.
- **Usage**: In synthesis, a noise gate is useful for suppressing noise-floor bleed from oscillators between notes, or for cleaning up a `WHITE_NOISE` voice so it is silent between triggered events. As a global FX it removes tape-style hiss from sustained pauses.
- **Status**: Planned.

### Envelope Follower
- **Type name**: `ENVELOPE_FOLLOWER`
- **Purpose**: Extracts a dynamic control signal from an audio input.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_CONTROL` out `envelope_out` (unipolar)
- **Parameters**: `attack` (0.0–1.0s), `release` (0.0–2.0s)
- **Status**: Planned.

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
- **Status**: Planned.

### CV Mixer / Attenuverter
- **Type name**: `CV_MIXER`
- **Purpose**: Combines, scales, and inverts control signals before routing to a destination. Negative mix weights implement inversion.
- **Ports**:
  - `PORT_CONTROL` in `cv_in_1` … `cv_in_4` (bipolar)
  - `PORT_CONTROL` out `cv_out` (bipolar)
- **Parameters**: `gain_1` … `gain_4` (−1.0–1.0), `offset` (−1.0–1.0, default 0.0)
- **Bias / DC Offset**: The `offset` parameter injects a constant DC level into the output signal. This is the electronic equivalent of adding a bias voltage and is used to shift a bipolar signal (e.g. LFO output in [−1, 1]) into unipolar territory so a single VCA can produce tremolo without fully gating off on the negative LFO half-cycle. Example: `offset=0.5` + `gain_1=0.5` on a full-range bipolar LFO yields a unipolar tremolo depth in [0, 1].
- **Delayed Vibrato**: Route LFO `control_out` → `CV_MIXER` `cv_in_1`. Route a second `ADSR_ENVELOPE` `envelope_out` → `CV_MIXER` `cv_in_2` (acts as VCA for the LFO signal). The slow-attack envelope ramps the LFO gain from zero, producing vibrato that only appears after the note onset — the characteristic technique for bowed-string and woodwind patches. Connect `CV_MIXER` `cv_out` → `VCO` `pitch_cv`.
- **Status**: Planned.

### CV Splitter
- **Type name**: `CV_SPLITTER`
- **Purpose**: Fans one control signal out to up to four destinations, each with independent gain scaling. Unity-gain fan-out is the default; unused outputs are ignored.
- **Ports**:
  - `PORT_CONTROL` in `cv_in` (bipolar)
  - `PORT_CONTROL` out `cv_out_1` … `cv_out_4` (bipolar)
- **Parameters**: `gain_1` … `gain_4` (−2.0–2.0, default 1.0 each)
- **Usage**: Patch one ADSR `envelope_out` → `CV_SPLITTER` `cv_in`, then connect `cv_out_1` → VCA `gain_cv` and `cv_out_2` → VCF `cutoff_cv` to drive both amplitude and filter shaping from a single envelope without requiring a `CV_MIXER`. Set `gain_2` to scale the filter modulation depth independently of the amplitude path.
- **Status**: Planned.

### S&H (Sample & Hold)
- **Type name**: `SAMPLE_HOLD`
- **Purpose**: Stepped modulation — freezes an input CV at each clock trigger.
- **Ports**:
  - `PORT_CONTROL` in `cv_in` (bipolar)
  - `PORT_CONTROL` in `clock_in` (unipolar)
  - `PORT_CONTROL` out `cv_out` (bipolar)
- **Usage**: Patch a noise source (or LFO sawtooth) into `cv_in` and an LFO square wave into `clock_in`. The output steps to a new random value at each clock edge, producing random pitch patterns when connected to `VCO` `pitch_cv` or random filter-color sweeps when connected to VCF `cutoff_cv`. Classic for wind/surf textures and random arpeggio effects.
- **Status**: Planned.

### Gate Delay / Pulse Shaper
- **Type name**: `GATE_DELAY`
- **Purpose**: Delays a gate event by a fixed time before passing it to a downstream module. Used to create multi-stage pitch effects (e.g. wolf-whistle: a second VCO gate fires delayed, producing a pitch that glides upward after note onset) and for tape-pulse reconstruction where gate edges must be offset in time.
- **Ports**:
  - `PORT_CONTROL` in `gate_in` (unipolar — **lifecycle port**, driven by VoiceContext)
  - `PORT_CONTROL` out `gate_out` (unipolar)
- **Parameters**: `delay_time` (0.0–2.0s, default 0.0)
- **Behavioral Rule**: When `gate_in` transitions high, `gate_out` remains low until `delay_time` has elapsed, then goes high for the remainder of the gate duration. If the gate closes before `delay_time` elapses, `gate_out` never fires. On `gate_in` low, `gate_out` drops low immediately (no trailing pulse).
- **Status**: Planned.

### Inverter
- **Type name**: `INVERTER`
- **Purpose**: Inverts a control signal: `out = scale * in` where `scale` defaults to −1.0. Used to produce counter-sweeping modulation — when an ADSR drives amplitude up, an inverted copy drives filter cutoff down, creating the characteristic harpsichord or plucked-string timbral signature.
- **Ports**:
  - `PORT_CONTROL` in `cv_in` (bipolar)
  - `PORT_CONTROL` out `cv_out` (bipolar)
- **Parameters**: `scale` (−2.0–2.0, default −1.0; allows attenuation alongside inversion)
- **Harpsichord usage**: Route `ADSR_ENVELOPE` `envelope_out` → `INVERTER` `cv_in`. Route `INVERTER` `cv_out` → VCF `cutoff_cv`. Route `ADSR_ENVELOPE` `envelope_out` directly → VCA `gain_cv`. The amplitude envelope and the inverted filter sweep are driven by the same ADSR simultaneously: as the note attacks and sustains the VCA opens while the filter closes, then as the note releases the VCA closes and the filter opens — the classic harpsichord brightness-on-decay.
- **Gong / metallic usage**: Route ring-mod envelope output through an `INVERTER` into a second resonant filter's cutoff to produce pitch-descending metallic decay.
- **Status**: Planned.

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

### Reverb
- **Type name**: `REVERB`
- **Purpose**: Simulated room reverberation — the acoustic impression of a physical space. Essential for giving synthesized voices a natural-sounding environment (Roland Recording §2-6: "the addition of reverberation to the music is extremely important for creating a natural sounding environment for the music"). Controls apparent distance between instrument and listener when blended with the dry signal.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: `time` (0.1–10.0s — RT60 reverberation time), `pre_delay` (0.0–0.1s — time before first reflection onset), `damping` (0.0–1.0 — high-frequency absorption; higher values simulate soft/carpeted rooms), `mix` (0.0–1.0, dry/wet blend; default 0.3)
- **Behavioral Logic**: Implemented as a Schroeder-style network of allpass and comb filters, or an FDN (Feedback Delay Network). The wet output is the diffuse reverberant tail; `mix` blends it with the dry signal. `pre_delay` controls the gap before reflections arrive, simulating listener-source distance. High `damping` values attenuate high-frequency content in the tail, approximating absorption by furnishings and walls.
- **Architecture Note**: Architecturally a **global FX module** — one instance processes the summed voice output, not a per-voice chain node. Per-voice instantiation is a temporary workaround pending global FX chain support. `SPATIAL` module `distance` logic should route to reverb send level: high distance = high reverb send, low direct level.
- **Status**: Planned.

### Echo / Delay
- **Type name**: `ECHO_DELAY`
- **Purpose**: Time-based repetition and echo, including BBD-style modulated delay for metallic shimmer effects (e.g. cymbal, bell sustain).
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out`
  - `PORT_CONTROL` in `time_cv` (unipolar)
  - `PORT_CONTROL` in `feedback_cv` (unipolar)
- **Parameters**: `time` (0.0–2.0s), `feedback` (0.0–0.99), `mix` (0.0–1.0), `mod_rate` (0.1–20 Hz, default 0.0 — rate of the built-in LFO that sweeps delay time; 0.0 disables modulation), `mod_intensity` (0.0–1.0, default 0.0 — depth of the delay-time sweep; at 1.0 the delay time oscillates ±50% of `time` at `mod_rate`, producing the metallic shimmer used in the Roland Cymbal patch, Vol 2 §3-5, Fig 3-16)
- **Note**: Feedback connection from `audio_out` back to `audio_in` must be marked `"feedback": true` in the patch. The executor uses the previous block's output to break the cycle.
- **BBD Cymbal usage**: Set `time` ≈ 20–40 ms, `feedback` ≈ 0.5–0.7, `mod_rate` ≈ 5–8 Hz, `mod_intensity` ≈ 0.3–0.6. The wobbling delay time produces the characteristic shimmering metallic decay of a struck cymbal or gong. Combine with `WHITE_NOISE` → `MOOG_FILTER` (high cutoff, moderate resonance) → `ECHO_DELAY` → percussive `VCA`.
- **Status**: Planned.

---

## 6. Global I/O

### Summing Mixer (Source Mixer)
- **Type name**: `SOURCE_MIXER`
- **Purpose**: Combines multiple audio signals before processing. Head node for multi-oscillator voices.
- **Ports**:
  - `PORT_AUDIO` in `audio_in_1` … `audio_in_N`
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: `gain_1` … `gain_N` (0.0–1.0 each)
- **Note**: Embedded inside `COMPOSITE_GENERATOR`. Also available as a standalone node.

### Audio Splitter
- **Type name**: `AUDIO_SPLITTER`
- **Purpose**: Fans one audio signal out to up to four destinations, each with independent gain scaling. Exact counterpart to `CV_SPLITTER` for the `PORT_AUDIO` domain. Required wherever a single audio source must feed multiple destinations — for example, when a VCO must simultaneously drive a `RING_MOD` input AND the main signal chain, or when the same oscillator feeds two parallel VCF branches.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out_1` … `audio_out_4`
- **Parameters**: `gain_1` … `gain_4` (0.0–2.0, default 1.0 each)
- **Usage**: Patch a VCO `audio_out` → `AUDIO_SPLITTER` `audio_in`, then connect `audio_out_1` → main chain (filter/VCA path) and `audio_out_2` → `RING_MOD` `audio_in_b` to use the same oscillator as both signal and FM modulator source. Also required for parallel-VCF topologies (two independent filters receiving the same oscillator).
- **Status**: Planned.

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
  - **Bell patch**: VCO1 (sine, fundamental) → `audio_in_a`; VCO2 (sine, non-integer ratio e.g. 2.756×) → `audio_in_b`; RING_MOD output → percussive VCA envelope.
  - **Choral/vocal formant patch**: one VCO + one VCO tuned to a voice-tract formant frequency → RING_MOD → VCA.
- **Status**: Planned.

### Audio Input
- **Type name**: `AUDIO_INPUT`
- **Purpose**: Interface for external audio signals (e.g. side-chain, vocoder source).
- **Ports**:
  - `PORT_AUDIO` out `audio_out`
- **Note**: Requires audio driver input buffer support. Pending audio driver input capability (Phase 18).
- **Status**: Planned.

---

## Implementation Rules

- **Port type enforcement**: `bake()` must reject any connection where a `PORT_AUDIO` output is connected to a `PORT_CONTROL` input, or vice versa.
- **Generator-first rule**: `bake()` must verify that the first node in `signal_chain_` outputs `PORT_AUDIO`.
- **VCA/Envelope separation**: `AdsrEnvelopeProcessor` produces `PORT_CONTROL` output only. A separate `VcaProcessor` node performs audio multiplication via its `gain_cv` input. They must not be merged.
- **Lifecycle ports**: `gate_in` and `trigger_in` are injected by `VoiceContext`, not wired via `connections_`. `bake()` skips connection validation for these port names.
- **Feedback connections**: Must be marked `"feedback": true`. The executor uses the previous block's output for these connections.
- **Multiple inputs**: A node with multiple input ports (Ring Modulator, CV Mixer, SourceMixer) has all inputs gathered before `pull()` is called. The executor must resolve all inputs before executing any node. Multi-input execution in `Voice::pull_mono` is not yet implemented; it will land in the bell/ring-mod phase.
- **Dynamic routing**: Modules are not hardcoded in `Voice`. `Voice` manages two lists — `signal_chain_` (PORT_AUDIO nodes) and `mod_sources_` (PORT_CONTROL generators) — with instance tags (e.g. `"VCO"`, `"ENV"`, `"LFO1"`) for parameter targeting and port connection. `add_processor()` routes each node automatically based on its output port type.
- **Global vs per-voice**: Per-voice modules live in `signal_chain_` or `mod_sources_`. Global modules (chorus, reverb, master bus) live in a separate global FX chain applied after voice summing. Do not instantiate global modules per-voice.
- **Sample rate**: All internal timing (ADSR curves, LFO rates, delay times) must derive from the runtime `sample_rate_` passed at construction. No hardcoded sample rate assumptions. Supported rates: 44100 Hz and 48000 Hz. If hardware reports a rate above 48000, the engine negotiates down to 48000.
- **Filter chain placement**: All four filter types (`MOOG_FILTER`, `DIODE_FILTER`, `SH_FILTER`, `MS20_FILTER`) are first-class chain nodes. Add them via `engine_add_module("MOOG_FILTER", "VCF")` and wire audio with `engine_connect_ports`. `Voice` no longer contains an internal `filter_` member — the hardcoded fallback path has been removed. Minimum filter chain: `COMPOSITE_GENERATOR → MOOG_FILTER → ADSR_ENVELOPE → VCA`.
- **Per-tag parameter addressing**: Parameters are addressed by tag in the patch JSON using a nested object per tag (see PATCH_SPEC.md §Parameters Object). At runtime, `set_named_parameter` must be extended to accept an optional tag scope so that two nodes of the same type (e.g. `"VCO1"` and `"VCO2"`) can be addressed independently. The patch loader maps `parameters["VCO2"]["detune"]` to `voice.find_by_tag("VCO2")->apply_parameter("detune", value)`. Full centralised ramping is Phase 21 (ParameterManager); tag-scoped direct addressing should land with multi-oscillator support.
- **Parallel signal paths**: Not yet supported. The current `Voice` has a single `signal_chain_`. For now, multiple oscillators share a `SOURCE_MIXER` node before a common filter+VCA. Independent per-oscillator filter chains are a future architecture goal.

---

## Known Gaps vs. Roland System 100M Techniques

The following capabilities are demonstrated in Roland's *Practical Synthesis* documentation and are either absent or incompletely specified in this module set. Each item references the technique and documents the current gap.

| Technique | Source | Gap | Resolution |
|-----------|--------|-----|------------|
| Hard VCO sync | Clarinet, lead synth patches | `COMPOSITE_GENERATOR` has no `sync_in` port; sync topology between two VCO instances not specified | Add `sync_in` port to `COMPOSITE_GENERATOR`; implement phase-reset on rising edge. Scoped to the bell/ring-mod phase alongside multi-input executor |
| Delayed vibrato | Violin, flute, bowed strings | CV_MIXER + second ADSR envelope required to gate the LFO signal; both `CV_MIXER` and per-tag parameter addressing are planned | `CV_MIXER` + Phase 21 ParameterManager |
| Portamento / glide | Lead and bass patches | `MATHS` is documented as a slew limiter / portamento device but is **planned** only; no pitch-dip (inverted envelope on pitch CV) pattern specified | Implement `MATHS`; add inverted-output parameter; document pitch-dip patch |
| Keyboard tracking of VCF cutoff | Nearly all tonal patches | `MOOG_FILTER` / `DIODE_FILTER` have a `kybd_cv` port but it is not wired by the current Voice architecture; the `Voice` does not pass keyboard pitch into filter CV | `Voice::pull_mono` must route note frequency as a bipolar CV to `kybd_cv` on each block |
| Pink noise | Wind, surf, rain patches | `WHITE_NOISE` `color` parameter is declared but implementation is planned | Implement −3dB/octave shelving filter chain in `WHITE_NOISE` |
| Gate delay (wolf-whistle) | Specialty pitch effects | `GATE_DELAY` module is now specified above; no implementation yet | Implement `GATE_DELAY` as a simple block-level countdown timer |
| Ring modulator | Bell, metallic, choral patches | `RING_MOD` is now specified above with behavioral logic; no implementation yet | Implement as sample-by-sample `A[n] * B[n]` in `RingModProcessor`; multi-input execution in `Voice` graph not yet wired |
| Filter as chain node | Pizzicato strings, two-VCF percussion, VCF self-oscillation, separate filter type per patch | ~~RESOLVED~~ All four filter types (`MOOG_FILTER`, `DIODE_FILTER`, `SH_FILTER`, `MS20_FILTER`) are now first-class chain nodes; `cutoff_cv` CV is routed via `apply_parameter` in `pull_mono`; `Voice::filter_` removed | — |
| Audio-rate filter FM | Tom Tom, Cow Bell, Bongo Drums patches | VCO `audio_out` → VCF cutoff modulation requires connecting a `PORT_AUDIO` output to a `PORT_CONTROL`-style cutoff input; current architecture rejects PORT_AUDIO→PORT_CONTROL connections at bake time | Add a dedicated `fm_in` PORT_AUDIO input to `MOOG_FILTER` and `DIODE_FILTER`; `fm_in` is summed into the cutoff frequency at audio rate with a configurable depth; already declared above |
| LFO as envelope gate source | Percussion Trill (Vol 2, Fig 3-13) | LFO square wave → ADSR `gate_in` is blocked because `gate_in` is a lifecycle port reserved for `VoiceContext` | Add `ext_gate_in` non-lifecycle PORT_CONTROL input to `ADSR_ENVELOPE`; OR'd with lifecycle `gate_in`; already declared above |
| Two VCAs in series | Percussion amplitude shaping (linear dynamics + exponential decay) | No known architecture gap — the chain model already supports sequential audio processors; a patch may include two `VCA` nodes in `chain` with different `response_curve` values; executor must process them in order | Verify executor handles multiple VCA nodes in `signal_chain_` correctly; no new module needed |
| Band Reject Filter topology | Notch/comb effects (Roland §5-7, Fig 5-7c) | Requires a LPF and HPF in **parallel** with outputs summed; the current `Voice` has a single serial `signal_chain_` — parallel branches are not yet supported | Extend `Voice` architecture to support forked/parallel signal paths; `BAND_REJECT_FILTER` module specified above; implementation deferred until parallel signal path support lands |
| VCO audio-rate pitch FM | §2-2, Fig 2-5 (two-VCO FM patch) | VCO-2 `audio_out` (PORT_AUDIO) → VCO-1 pitch modulation requires connecting PORT_AUDIO to a pitch input; `pitch_cv` is PORT_CONTROL only — cross-type connection rejected at bake time | Add `fm_in` PORT_AUDIO port to `COMPOSITE_GENERATOR` with `fm_depth` parameter; `fm_in` is scaled and summed into the pitch CV path at audio rate; already declared above |
| Ring mod LFO modulation | Vol 1 §2-5, Fig 2-19 (Violin Bowed Tremolo) | LFO `control_out` (PORT_CONTROL) → `RING_MOD` `audio_in_b` (PORT_AUDIO) is a port-type violation; bake rejects it; no control-rate input exists on `RING_MOD` | Add `mod_in` PORT_CONTROL input to `RING_MOD`; when connected, the control signal multiplies `audio_in_a` directly — at LFO rates this is AM tremolo, at audio rates it is ring modulation; already declared above |
