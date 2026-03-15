# Module Descriptions: Musical Toolbox

This document defines the functional requirements for all DSP processors. All modules must adhere to the `PORT_AUDIO` and `PORT_CONTROL` protocol.

## 1. Generators (Oscillators & Noise)

* **VCO (Voltage Controlled Oscillator)**
* **Purpose**: Primary periodic harmonic generation.
* **Ports**: `PORT_CONTROL` (Pitch CV, PWM CV, Sync In); `PORT_AUDIO` (Output).
* **Logic**: Exponential frequency response ($f = f_0 \cdot 2^{CV}$).


* **LFO (Low Frequency Oscillator)**
* **Purpose**: Sub-audio modulation source.
* **Ports**: `PORT_CONTROL` (Rate CV, Reset); `PORT_AUDIO` (Output).


* **Noise Generator**
* **Purpose**: Stochastic signal generation.
* **Ports**: `PORT_AUDIO` (Output).
* **Modes**: White (flat) and Pink (-3dB/octave) noise.



## 2. Filters (Timbre Shaping)

* **VCF (Voltage Controlled Filter)**
* **Purpose**: Subtractive frequency manipulation.
* **Ports**: `PORT_AUDIO` (Input, Output); `PORT_CONTROL` (Cutoff CV, Res CV).
* **Modes**: Switchable Low-Pass (24dB), High-Pass, Band-Pass, and Notch.


* **Resonator**
* **Purpose**: Emulation of physical acoustic body resonances/cavities.
* **Ports**: `PORT_AUDIO` (Input, Output); `PORT_CONTROL` (Freq CV).



## 3. Amplitude & Dynamics

* **VCA (Voltage Controlled Amplifier)**
* **Purpose**: Gain control and output stage.
* **Ports**: `PORT_AUDIO` (Input, Output); `PORT_CONTROL` (Gain CV).
* **Behavior**: Switchable Linear (for modulation) and Exponential (for loudness) response.


* **Envelope Generator (ADSR/AD)**
* **Purpose**: Transient shaping.
* **Ports**: `PORT_CONTROL` (Gate In, Trigger In, Envelope Out).


* **Envelope Follower**
* **Purpose**: Extraction of dynamic control signals from audio inputs.
* **Ports**: `PORT_AUDIO` (Input); `PORT_CONTROL` (Envelope Out).



## 4. Modulation & CV Utilities

* **Maths Function Generator**
* **Purpose**: Versatile envelope, slew limiter, or LFO.
* **Ports**: `PORT_CONTROL` (Input, Output).
* **Behavior**: Implements complex curves (Log/Linear/Exponential).


* **CV Mixer/Attenuverter**
* **Purpose**: Combining and inverting control signals.
* **Ports**: `PORT_CONTROL` (Inputs, Output).


* **S&H (Sample & Hold)**
* **Purpose**: Stepped modulation generation.
* **Ports**: `PORT_CONTROL` (Input, Clock/Trigger, Output).



## 5. Effects & Spatial

* **Spatial Processor**
* **Purpose**: Panning and stereo field positioning.
* **Ports**: `PORT_AUDIO` (In, Left Out, Right Out); `PORT_CONTROL` (Pan CV).


* **Echo/Delay**
* **Purpose**: Time-based reverberation and echo.
* **Ports**: `PORT_AUDIO` (Input, Output); `PORT_CONTROL` (Time/Feedback CV).



## 6. Global I/O

* **Audio Input**
* **Purpose**: Interface for external signals.
* **Ports**: `PORT_AUDIO` (Output).


* **Summing Mixer**
* **Purpose**: Final aggregation of signal chain output.
* **Ports**: `PORT_AUDIO` (Inputs, Output).


* **Ring Modulator**
* **Purpose**: Non-linear frequency interaction.
* **Ports**: `PORT_AUDIO` (In A, In B, Output).



---

* **Normalization**: All `PORT_AUDIO` signals are treated as `[-1.0, 1.0]`. All `PORT_CONTROL` signals are treated as normalized units (`[-1.0, 1.0]` bipolar or `[0.0, 1.0]` unipolar).
* **Validation**: The `bake()` step in the `Voice` must ensure that a `Generator` type node occupies the head of the `signal_chain_` to guarantee buffer clearing.
* **Dynamic Routing**: These modules are not hardcoded in the `Voice`. Instead, the `Voice` manages a `std::vector` of these processor types, which are assigned tags (e.g., `"VCO_1"`, `"VCF_HP"`) for `set_param` and modulation linking.

