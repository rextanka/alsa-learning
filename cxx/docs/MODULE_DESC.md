# Module Descriptions: Musical Toolbox

This document defines the functional requirements for all DSP processors.

---

## Port Type System

All modules declare their ports using two types:

- **`PORT_AUDIO`** — an audio-rate signal carrying sound. Range: `[-1.0, 1.0]`. Connects only to `PORT_AUDIO` inputs.
- **`PORT_CONTROL`** — an audio-rate signal carrying modulation or CV. Range: `[-1.0, 1.0]` bipolar or `[0.0, 1.0]` unipolar. Connects only to `PORT_CONTROL` inputs.

Both port types run at **audio rate** — a full `std::span<float>` per block. The distinction is semantic and enforced at graph construction time, not by a different sample rate.

Each port has a **tag** (a unique string name on that module instance, e.g. `"pitch_cv"`, `"audio_out"`) used by the graph to form named connections. `bake()` validates that all connections match port types before the voice becomes active.

---

## 1. Generators (Oscillators & Noise)

* **VCO (Voltage Controlled Oscillator)**
* **Purpose**: Primary periodic harmonic generation.
* **Ports**:
  - `PORT_CONTROL` in: `pitch_cv`, `pwm_cv`, `sync_in`
  - `PORT_AUDIO` out: `sine_out`, `tri_out`, `saw_out`, `pulse_out`
* **Logic**: Exponential frequency response ($f = f_0 \cdot 2^{CV}$). All four waveforms are produced simultaneously; downstream SourceMixer nodes select and blend them.
* **Waveforms**: Sine, Triangle, Sawtooth, Pulse (square/PWM). Triangle wave has only odd harmonics at squared-amplitude rolloff (A/n²), giving a near-sine timbre. Sawtooth has all harmonics at 1/n rolloff.


* **LFO (Low Frequency Oscillator)**
* **Purpose**: Sub-audio modulation source. Produces a control signal for modulating other parameters.
* **Ports**:
  - `PORT_CONTROL` in: `rate_cv`, `reset`
  - `PORT_CONTROL` out: `control_out`
* **Waveforms**: Sine (vibrato, tremolo), Triangle (linear ramp modulation), Sawtooth, Square, S&H (random stepped modulation). Waveform is a configuration parameter, not a port.
* **Note**: LFO output is `PORT_CONTROL`, not `PORT_AUDIO`. It is a modulation source and must not be patched directly into an audio mix.


* **Noise Generator**
* **Purpose**: Stochastic signal generation.
* **Ports**:
  - `PORT_AUDIO` out: `audio_out`
* **Modes**: White (flat) and Pink (-3dB/octave) noise.



## 2. Filters (Timbre Shaping)

* **VCF (Voltage Controlled Filter)**
* **Purpose**: Subtractive frequency manipulation.
* **Ports**:
  - `PORT_AUDIO` in: `audio_in`
  - `PORT_AUDIO` out: `audio_out`
  - `PORT_CONTROL` in: `cutoff_cv`, `res_cv`, `kybd_cv`
* **Modes**: Switchable Low-Pass (24dB/oct), High-Pass (12dB/oct), Band-Pass (12dB/oct), and Notch.
* **Keyboard tracking**: `kybd_cv` accepts the same 1V/octave pitch CV as the VCO. The VCF must apply the same exponential voltage-to-frequency response as the VCO so the cutoff point can track pitch. A tracking amount parameter (0.0–1.0) scales how closely the cutoff follows the keyboard.
* **Self-oscillation**: At maximum resonance the filter becomes a sine wave oscillator at the cutoff frequency. This is a supported implementation behaviour.


* **Resonator**
* **Purpose**: Emulation of physical acoustic body resonances/cavities.
* **Ports**:
  - `PORT_AUDIO` in: `audio_in`
  - `PORT_AUDIO` out: `audio_out`
  - `PORT_CONTROL` in: `freq_cv`



## 3. Amplitude & Dynamics

* **VCA (Voltage Controlled Amplifier)**
* **Purpose**: Gain control and output stage. A dedicated audio gain node — separate from the Envelope Generator.
* **Ports**:
  - `PORT_AUDIO` in: `audio_in`
  - `PORT_AUDIO` out: `audio_out`
  - `PORT_CONTROL` in: `gain_cv`, `initial_gain_cv`
* **Behavior**: Switchable Linear (for modulation) and Exponential (for loudness) response.
* **Initial gain**: `initial_gain_cv` sets the resting DC level around which `gain_cv` modulates. Required for LFO tremolo: without a non-zero initial gain, a bipolar LFO signal would only open the VCA on positive half-cycles. When `initial_gain_cv` is not patched, the VCA defaults to fully closed (0.0) so that only envelope-driven gain is heard.
* **Note**: The VCA and Envelope Generator are distinct nodes. The Envelope Generator produces a `PORT_CONTROL` signal that is patched into the VCA's `gain_cv` input. They must not be merged into a single processor.


* **Envelope Generator (ADSR/AD)**
* **Purpose**: Transient shaping. Produces a control signal driven by gate events.
* **Ports**:
  - `PORT_CONTROL` in: `gate_in`, `trigger_in`
  - `PORT_CONTROL` out: `envelope_out`
* **Curve shape**: Attack, Decay, and Release segments use **exponential curves** by default (perceptually natural). Linear mode is selectable for special effects. Sustain is a fixed level (no curve).
* **Note**: Output is `PORT_CONTROL` only. To shape audio amplitude, patch `envelope_out` → VCA `gain_cv`. To shape filter cutoff, patch `envelope_out` → VCF `cutoff_cv`.


* **Envelope Follower**
* **Purpose**: Extraction of dynamic control signals from audio inputs.
* **Ports**:
  - `PORT_AUDIO` in: `audio_in`
  - `PORT_CONTROL` out: `envelope_out`



## 4. Modulation & CV Utilities

These modules operate entirely in the control domain. They require `PORT_CONTROL` connections to be useful and are first-class nodes in the `signal_chain_`.

* **Maths Function Generator**
* **Purpose**: Versatile envelope, slew limiter, or LFO with configurable curve shapes.
* **Ports**:
  - `PORT_CONTROL` in: `cv_in`
  - `PORT_CONTROL` out: `cv_out`
* **Behavior**: Implements Log/Linear/Exponential curves.
* **Portamento use case**: When patched between keyboard pitch CV and VCO `pitch_cv`, this module acts as a portamento/glide circuit — a slew limiter that smooths stepped keyboard CV into a gliding pitch transition. Portamento rate is controlled by the rise/fall time parameters.


* **CV Mixer/Attenuverter**
* **Purpose**: Combining, scaling, and inverting control signals before routing to a destination.
* **Ports**:
  - `PORT_CONTROL` in: `cv_in_1`, `cv_in_2`, `cv_in_3`, `cv_in_4`
  - `PORT_CONTROL` out: `cv_out`
* **Note**: Negative mix weights implement signal inversion (attenuverter behaviour).


* **S&H (Sample & Hold)**
* **Purpose**: Stepped modulation — freezes an input CV at each clock trigger.
* **Ports**:
  - `PORT_CONTROL` in: `cv_in`, `clock_in`
  - `PORT_CONTROL` out: `cv_out`



## 5. Effects & Spatial

* **Spatial Processor**
* **Purpose**: Panning and stereo field positioning.
* **Ports**:
  - `PORT_AUDIO` in: `audio_in`
  - `PORT_AUDIO` out: `left_out`, `right_out`
  - `PORT_CONTROL` in: `pan_cv`


* **Echo/Delay**
* **Purpose**: Time-based repetition and echo.
* **Ports**:
  - `PORT_AUDIO` in: `audio_in`
  - `PORT_AUDIO` out: `audio_out`
  - `PORT_CONTROL` in: `time_cv`, `feedback_cv`



## 6. Global I/O

* **Audio Input**
* **Purpose**: Interface for external audio signals entering the graph.
* **Ports**:
  - `PORT_AUDIO` out: `audio_out`


* **Summing Mixer (Source Mixer)**
* **Purpose**: Combines multiple audio signals before processing. Head node for multi-oscillator voices.
* **Ports**:
  - `PORT_AUDIO` in: `audio_in_1` … `audio_in_N`
  - `PORT_AUDIO` out: `audio_out`


* **Ring Modulator**
* **Purpose**: Non-linear frequency interaction between two audio signals.
* **Ports**:
  - `PORT_AUDIO` in: `audio_in_a`, `audio_in_b`
  - `PORT_AUDIO` out: `audio_out`



---

## Implementation Rules

* **Port type enforcement**: `bake()` must reject any connection where a `PORT_AUDIO` output is connected to a `PORT_CONTROL` input, or vice versa.
* **Generator-first rule**: `bake()` must verify that the first node in `signal_chain_` has a `PORT_AUDIO` out (i.e. is a Generator or SourceMixer).
* **VCA/Envelope separation**: The `AdsrEnvelopeProcessor` must not multiply audio in-place. It produces `PORT_CONTROL` output only. A separate `VcaProcessor` node performs the audio multiplication using its `gain_cv` input.
* **Dynamic Routing**: Modules are not hardcoded in the `Voice`. The `Voice` manages a `std::vector` of processor nodes, assigned instance tags (e.g. `"VCO_1"`, `"VCF_HP"`, `"ENV_AMP"`) for parameter targeting and port connection.
* **Tagging**: Each port on each node instance has a unique tag. Connections are formed by specifying `{source_node_tag, port_tag} → {dest_node_tag, port_tag}`.
