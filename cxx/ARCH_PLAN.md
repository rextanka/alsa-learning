# C++20 Block-Based DSP Architecture Plan

**Default plan for the `cxx/` engine.** This document is the single source of truth for architecture, contract, migration phases, and design decisions. Check it into git and keep it updated as we implement.

---

## Design Manifesto

### Philosophy
Create a portable, lightweight **'Musical Toolbox'** for creative exploration and **'Sound Toy'** development. The library is designed to be approachable and educational, allowing developers to easily build small musical programs and understand core DSP concepts.

### Target Platforms
- **macOS**: Tahoe 26.3+
- **Windows**: 11
- **Linux**: Fedora 42+, Ubuntu 22.04+

### Technical Pillars
- **Pull-Based Heartbeat**: Sample-accurate timing driven by the `AudioDriver`. Output pulls from the graph; processors pull from their inputs.
- **Mono-until-Stereo**: Keep signal paths mono for CPU efficiency until spatial effects or stereo-specific processing (panners/reverb) are required.
- **MPE & Microtonality**: Per-voice independence with a modular `TuningSystem` for cross-platform musical flexibility.
- **Centralized Zipper-Free Control**: A dedicated `ParameterManager` handles all ramping and smoothing "magic," hiding complexity from the toy developer and ensuring artifact-free parameter updates.

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    C Interface Layer                     │
│              (bridge/CInterface.h, AudioBridge.cpp)      │
└──────────────────────┬──────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────┐
│              VoiceManager (Polyphony) [planned]         │
│         ┌──────────────┐  ┌──────────────┐              │
│         │    Voice 1   │  │    Voice N   │              │
│         │  Graph      │  │  Graph       │              │
│         └──────────────┘  └──────────────┘              │
└──────────────────────┬──────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────┐
│              Processor base (audio/Processor.hpp)       │
│  Oscillators │ Envelope [planned] │ Filter [planned]     │
└──────────────────────┬──────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────┐
│              AudioDriver HAL (hal/include) [planned]    │
│         ALSA │ CoreAudio │ WASAPI                       │
└─────────────────────────────────────────────────────────┘
```

---

## Node Contract (Pull + NVI)

- **InputSource**: Interface with `pull(std::span<float> output, const VoiceContext* voice_context = nullptr)`.
- **Processor**: Inherits `InputSource`; public `pull()` starts/stops profiler and calls `do_pull()`. Subclasses implement **`do_pull()`** and **`reset()`**.
- **Oscillators**: Source nodes (no inputs). Base `OscillatorProcessor::do_pull()` loops over the span, calls `update_frequency_ramp()` then `generate_sample()` per sample. Subclasses implement `generate_sample()` and `reset_oscillator_state()`.
- **VoiceContext**: Optional query pattern for velocity/aftertouch/current note; processors may ignore it.
- **Profiling**: Compile-time `AUDIO_ENABLE_PROFILING`; when off, zero cost. `get_metrics()` returns last/max ns and total blocks.

---

## File Structure

**Current (implemented):**

```
cxx/
├── audio/
│   ├── InputSource.hpp
│   ├── VoiceContext.hpp
│   ├── PerformanceProfiler.hpp
│   ├── Processor.hpp
│   └── oscillator/
│       ├── OscillatorProcessor.hpp
│       ├── SineOscillatorProcessor.hpp
│       ├── SquareOscillatorProcessor.hpp
│       ├── SawtoothOscillatorProcessor.hpp
│       ├── TriangleOscillatorProcessor.hpp
│       └── WavetableOscillatorProcessor.hpp   # One class, shape at create (Sine/Saw/Square/Triangle)
├── bridge/
│   ├── CInterface.h
│   └── AudioBridge.cpp
├── hal/
│   └── include/
│       └── AudioDriver.hpp
├── CMakeLists.txt
├── main.cpp
└── ARCH_PLAN.md
```

**Planned:**

```
cxx/audio/
├── envelope/
│   ├── EnvelopeProcessor.hpp
│   ├── ADSREnvelopeProcessor.hpp
│   └── ADEnvelopeProcessor.hpp
├── Graph.hpp
├── Voice.hpp
└── VoiceManager.hpp
cxx/hal/
├── null/NullAudioDriver.hpp
├── file/FileAudioDriver.hpp
├── alsa/, coreaudio/, wasapi/
```

---

## C API Factory (Oscillators)

- **Algorithm-based** (rotor/PolyBLEP): `OSC_SINE`, `OSC_SQUARE`, `OSC_TRIANGLE`, `OSC_SAWTOOTH`.
- **Wavetable-based** (one class, shape at create): `OSC_WAVETABLE` (= `OSC_WAVETABLE_SINE`), `OSC_WAVETABLE_SINE`, `OSC_WAVETABLE_SAW`, `OSC_WAVETABLE_SQUARE`, `OSC_WAVETABLE_TRIANGLE`.

`oscillator_create(type, sample_rate)` returns an opaque handle; all other APIs (set_frequency, process, reset, get_metrics, destroy) are unchanged. Swift/.NET need not care whether the backend is algorithm or wavetable.

---

## Migration Phases

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Processor base, PerformanceProfiler, InputSource, VoiceContext | Done |
| 2 | OscillatorProcessor + Sine, Square, Saw, Triangle + Wavetable (do_pull contract) | Done |
| 3 | C bridge: oscillator_create/factory for all types (incl. wavetable shapes) | Done |
| 4 | EnvelopeProcessor base + ADSR/AD | Planned |
| 5 | Graph (pull model, buffer pool, optional feedback) | Planned |
| 6 | Voice + VoiceManager (polyphony, stealing, query pattern) | Planned |
| 7 | AudioDriver HAL (null/file, then ALSA/CoreAudio/WASAPI) | Planned |
| 8 | Integration tests, performance validation | Done |
| 9 | API Maturity & Documentation | In Progress |
| 10 | **Musical Logic**: 960 PPQ `MusicalClock`, `TuningSystem` (Microtonality), and human-readable `Note('C4')` abstractions. | Planned |
| 11 | **MPE & Scheduler**: MPE support, **ParameterManager**, and 'Safe Update' thread-safe command buffer for UI-to-Audio batch changes. | Planned |
| 12 | **MIDI Integration**: MIDI HAL for Linux/Mac/Win with CC mapping and SysEx hooks. | Planned |
| 13 | **Optimization**: SIMD, fast-math, and dynamic 'Mono-to-Stereo' negotiation to maximize polyphony. | Planned |

---

## Key Design Decisions

- **Block size**: Configurable (not hardcoded); default 1024.
- **Format**: Float (-1..1) internally; convert at HAL boundary.
- **Buffer ownership**: Graph owns buffers; nodes use spans (zero-copy).
- **Polyphony**: Compile-time `AUDIO_MAX_VOICES` (e.g. 16); runtime cap optional.
- **Voice stealing**: Prefer releasing voice (longest first), else active (e.g. lowest note).
- **Per-voice params**: Velocity and aftertouch via **query pattern** (VoiceContext in `pull()`).
- **Wavetable**: Single `WavetableOscillatorProcessor` class; shape selected at create.
- **10ms MMA Latency Target**: Optimize buffer sizes and processing for consistent real-time response.
- **Nord-style Modular Routing**: Supporting "Audio-as-Control" where audio signals can be used as modulation sources across the graph.

---

## References

- Full historical plan (optional): `.cursor/plans/c++20_block-based_dsp_architecture_4b3bff39.plan.md`
- Project rules: repo root `.cursorrules` (branch naming, C++20/23, bin output, C interop).
