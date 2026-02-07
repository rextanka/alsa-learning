# ALSA Audio Learning

This repository documents a journey from basic Linux sound programming to building a functional, ADSR-enabled monophonic synthesizer on Fedora Linux. 

The project is optimized for a **Razer Laptop (Intel PCH / Realtek ALC298)** hardware profile, targeting 48kHz Stereo S16_LE audio output using the ALSA (Advanced Linux Sound Architecture) API.

---

## 📂 Project Structure

### 1. `hello-alsa/`
**Focus: Foundations of ALSA PCM API**
The "Hello World" of audio programming. These utilities test hardware communication and basic signal generation.
* **Key Features**: PCM handle configuration, hardware parameter setting (format, channels, rate), and raw buffer writing via `snd_pcm_writei`.
* **Goal**: Mastering the boilerplate required to talk to the Linux sound kernel.

### 2. `osc_cli/`
**Focus: Modular Synthesis & Command Line Control**
The first step toward a modular synthesizer architecture, separating signal generation from hardware output.
* **Key Features**: 
    * Multiple Waveforms: Sine, Square, Triangle, and Sawtooth.
    * Frequency Ramping: Linear transitions between frequencies over time.
    * CLI Control: Dynamic parameter adjustment via `getopt`.
* **Goal**: Managing real-time parameter changes and modularizing audio C code.

### 3. `osc_cli_adsr/`
**Focus: Expressive Synthesis & The Voice Model**
The current core of the project, introducing musical contouring through an ADSR envelope.
* **Key Features**:
    * **State Machine**: Logic for Attack, Decay, Sustain, and Release phases.
    * **Exponential Curves**: Natural-sounding volume contours for Decay and Release.
    * **Anti-Click Logic**: Legato note-on support and sustain slewing to eliminate digital popping.
    * **Auto-Timing**: Intelligent duration calculation based on envelope parameters.
* **Goal**: Implementing non-linear gain control and complex state transitions.



---

## 🛠️ Build & Workflow
The project uses a root-level `Makefile` and targets the Razer Laptop's Realtek ALC298 codec.

* **Compiler**: `gcc`
* **Flags**: `-Wall -g -O0` (for debugging)
* **Libraries**: `-lasound -lm`
* **Branching**: Workflow follows a `yyyymmddhhmm` naming convention.

---

## 🚀 Future Roadmap

### Phase 1: Musical Infrastructure
* **MIDI Note Mapping**: Implement a lookup system to convert MIDI note numbers (0–127) to frequencies using equal temperament ($A4 = 440Hz$).
* **Voice Management**: Encapsulate the Oscillator and Envelope into a single `Voice` struct for easier event management.

### Phase 2: Signal Processing (Subtractive Synthesis)
* **Band-Limited Oscillators**: Implement PolyBLEP or similar methods to eliminate high-frequency aliasing on Square and Sawtooth waves.
* **Resonant Filters**: Add a Low-Pass Filter (LPF) to allow for classic "synth" timbre shaping.

### Phase 3: Interactivity
* **Real-time MIDI Input**: Integrate `libasound` MIDI sequencer API to support USB MIDI keyboards.
* **Ncurses UI**: Develop a terminal-based dashboard for real-time parameter visualization and control.

### Phase 4: Performance Optimization
* **SIMD Acceleration**: Utilize Intel SSE/AVX instructions for parallel sample processing.
* **Fixed-Point Math**: Explore optimization
