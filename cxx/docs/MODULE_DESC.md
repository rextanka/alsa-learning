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

Certain port names are **reserved lifecycle ports**. They are driven by the `VoiceContext` (note_on/note_off events) and must not be wired via `connections_`. `bake()` recognises and skips validation for these ports:

| Reserved port name | Signal |
|--------------------|--------|
| `gate_in`          | High while note is held, low on note_off |
| `trigger_in`       | One-block pulse on note_on |

### Feedback Connections

A connection that forms a cycle in the signal graph (e.g. delay feedback) must be marked `"feedback": true` in the patch. The graph executor uses the **previous block's output** for feedback connections, breaking the cycle without aliasing. Non-feedback connections form a DAG validated by `bake()`.

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
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: `saw_gain`, `pulse_gain`, `sine_gain`, `triangle_gain`, `sub_gain`, `wavetable_gain`, `noise_gain` (all 0.0–1.0), `pulse_width` (0.0–0.5), `wavetable_type` (int enum)
- **Logic**: Exponential frequency response (f = f₀ · 2^CV). All waveforms produced simultaneously, blended by internal SourceMixer. Sub-oscillator phase-coupled to pulse oscillator.

### LFO (Low Frequency Oscillator)
- **Type name**: `LFO`
- **Purpose**: Sub-audio modulation source.
- **Ports**:
  - `PORT_CONTROL` in `rate_cv` (unipolar)
  - `PORT_CONTROL` in `reset` (unipolar, lifecycle-style trigger)
  - `PORT_CONTROL` out `control_out` (bipolar)
- **Parameters**: `rate` (0.01–20 Hz), `intensity` (0.0–1.0), `waveform` (enum: Sine, Triangle, Saw, Square, S&H)
- **Note**: Output is `PORT_CONTROL`. Must not be patched directly into an audio mix.

### Noise Generator
- **Type name**: `WHITE_NOISE`
- **Purpose**: Stochastic signal generation.
- **Ports**:
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: none (embedded in `COMPOSITE_GENERATOR` as channel 6; also available standalone)
- **Modes**: White (flat). Pink (−3dB/octave) planned.

### Drawbar Organ
- **Type name**: `DRAWBAR_ORGAN`
- **Purpose**: Additive synthesis modelling a Hammond-style tonewheel organ. Nine sine oscillators at fixed harmonic ratios corresponding to drawbar footage (16', 8', 4', 2⅔', 2', 1⅗', 1½', 1⅓', 1').
- **Ports**:
  - `PORT_CONTROL` in `pitch_cv` (bipolar, 1V/oct)
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: `drawbar_16`, `drawbar_8`, `drawbar_4`, `drawbar_267`, `drawbar_2`, `drawbar_135`, `drawbar_113`, `drawbar_1` (all 0.0–1.0, unipolar), `percussion` (bool), `percussion_decay` (0.0–2.0s)
- **Note**: Gate-through operation is achieved by setting ADSR attack=0, decay=0, sustain=1.0, release short. Verified by Bach organ tests.

---

## 2. Filters (Timbre Shaping)

### VCF — Moog Ladder
- **Type name**: `MOOG_FILTER`
- **Purpose**: 4-pole Moog ladder low-pass filter (24dB/oct). Self-oscillates at max resonance.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out`
  - `PORT_CONTROL` in `cutoff_cv` (bipolar)
  - `PORT_CONTROL` in `res_cv` (unipolar)
  - `PORT_CONTROL` in `kybd_cv` (bipolar, 1V/oct keyboard tracking)
- **Parameters**: `cutoff` (20–20000 Hz, log), `resonance` (0.0–1.0)

### VCF — Diode Ladder
- **Type name**: `DIODE_FILTER`
- **Purpose**: Diode ladder filter with a characteristically aggressive resonance.
- **Ports**: same as `MOOG_FILTER`
- **Parameters**: same as `MOOG_FILTER`

### Resonator
- **Type name**: `RESONATOR`
- **Purpose**: Emulation of physical acoustic body resonances/cavities.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out`
  - `PORT_CONTROL` in `freq_cv` (bipolar)
- **Parameters**: `frequency` (20–20000 Hz, log), `decay` (0.0–4.0s)
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
- **Parameters**: `initial_gain` (0.0–1.0)
- **Behaviour**: Linear gain by default. `initial_gain_cv` sets resting DC level so a bipolar LFO can produce tremolo without fully closing the VCA on negative half-cycles.

### Envelope Generator (ADSR)
- **Type name**: `ADSR_ENVELOPE`
- **Purpose**: Transient shaping. Produces a control signal driven by gate events.
- **Ports**:
  - `PORT_CONTROL` in `gate_in` (unipolar — **lifecycle port**, driven by VoiceContext)
  - `PORT_CONTROL` in `trigger_in` (unipolar — **lifecycle port**)
  - `PORT_CONTROL` out `envelope_out` (unipolar)
- **Parameters**: `attack` (0.0–10.0s, log), `decay` (0.0–10.0s, log), `sustain` (0.0–1.0), `release` (0.0–10.0s, log)
- **Curve shape**: Exponential attack, decay, and release by default (perceptually natural).
- **Note**: Output is `PORT_CONTROL` only. To shape amplitude: patch `envelope_out` → VCA `gain_cv`. To shape filter: patch `envelope_out` → VCF `cutoff_cv`.

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
- **Parameters**: `gain_1` … `gain_4` (−1.0–1.0)
- **Status**: Planned.

### S&H (Sample & Hold)
- **Type name**: `SAMPLE_HOLD`
- **Purpose**: Stepped modulation — freezes an input CV at each clock trigger.
- **Ports**:
  - `PORT_CONTROL` in `cv_in` (bipolar)
  - `PORT_CONTROL` in `clock_in` (unipolar)
  - `PORT_CONTROL` out `cv_out` (bipolar)
- **Status**: Planned.

---

## 5. Effects & Spatial

### Juno Chorus
- **Type name**: `JUNO_CHORUS`
- **Purpose**: BBD-style stereo chorus emulating the Roland Juno-60 chorus circuit.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out`
- **Parameters**: `mode` (enum: Off, I, II, I+II), `rate` (0.1–10 Hz), `depth` (0.0–1.0)
- **Note**: Architecturally a **global FX module** — one instance processes the summed voice output, not a per-voice chain node. Per-voice instantiation is a temporary workaround pending global FX chain support (Phase 16).

### Spatial Processor
- **Type name**: `SPATIAL`
- **Purpose**: Panning and stereo field positioning.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `left_out`
  - `PORT_AUDIO` out `right_out`
  - `PORT_CONTROL` in `pan_cv` (bipolar)
- **Parameters**: `pan` (−1.0–1.0)

### Echo / Delay
- **Type name**: `ECHO_DELAY`
- **Purpose**: Time-based repetition and echo.
- **Ports**:
  - `PORT_AUDIO` in `audio_in`
  - `PORT_AUDIO` out `audio_out`
  - `PORT_CONTROL` in `time_cv` (unipolar)
  - `PORT_CONTROL` in `feedback_cv` (unipolar)
- **Parameters**: `time` (0.0–2.0s), `feedback` (0.0–0.99), `mix` (0.0–1.0)
- **Note**: Feedback connection from `audio_out` back to `audio_in` must be marked `"feedback": true` in the patch. The executor uses the previous block's output to break the cycle.
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

### Ring Modulator
- **Type name**: `RING_MOD`
- **Purpose**: Non-linear frequency interaction between two audio signals.
- **Ports**:
  - `PORT_AUDIO` in `audio_in_a`
  - `PORT_AUDIO` in `audio_in_b`
  - `PORT_AUDIO` out `audio_out`
- **Status**: Planned.

### Audio Input
- **Type name**: `AUDIO_INPUT`
- **Purpose**: Interface for external audio signals (e.g. side-chain, vocoder source).
- **Ports**:
  - `PORT_AUDIO` out `audio_out`
- **Note**: Requires audio driver input buffer support. Pending audio driver input capability (Phase 16).
- **Status**: Planned.

---

## Implementation Rules

- **Port type enforcement**: `bake()` must reject any connection where a `PORT_AUDIO` output is connected to a `PORT_CONTROL` input, or vice versa.
- **Generator-first rule**: `bake()` must verify that the first node in `signal_chain_` outputs `PORT_AUDIO`.
- **VCA/Envelope separation**: `AdsrEnvelopeProcessor` produces `PORT_CONTROL` output only. A separate `VcaProcessor` node performs audio multiplication via its `gain_cv` input. They must not be merged.
- **Lifecycle ports**: `gate_in` and `trigger_in` are injected by `VoiceContext`, not wired via `connections_`. `bake()` skips connection validation for these port names.
- **Feedback connections**: Must be marked `"feedback": true`. The executor uses the previous block's output for these connections.
- **Multiple inputs**: A node with multiple input ports (Ring Modulator, CV Mixer, SourceMixer) has all inputs gathered before `pull()` is called. The executor must resolve all inputs before executing any node.
- **Dynamic routing**: Modules are not hardcoded in `Voice`. The `Voice` manages a `std::vector` of processor nodes with instance tags (e.g. `"VCO"`, `"VCF_LP"`, `"ENV_AMP"`) for parameter targeting and port connection.
- **Global vs per-voice**: Per-voice modules live in `signal_chain_`. Global modules (chorus, reverb, master bus) live in a separate global FX chain applied after voice summing. Do not instantiate global modules per-voice.
- **Sample rate**: All internal timing (ADSR curves, LFO rates, delay times) must derive from the runtime `sample_rate_` passed at construction. No hardcoded sample rate assumptions. Supported rates: 44100 Hz and 48000 Hz. If hardware reports a rate above 48000, the engine negotiates down to 48000.
