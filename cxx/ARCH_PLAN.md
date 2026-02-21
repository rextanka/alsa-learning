# C++20 Block-Based DSP Architecture Plan

**Default plan for the `cxx/` engine.** This document is the single source of truth for architecture, contract, migration phases, and design decisions. Check it into git and keep it updated as we implement.

---

## Overview

Migrate from sample-by-sample C to a **block-based, pull-model** DSP engine in C++20:

- **Pull architecture**: Output pulls from the graph; processors pull from their inputs. Industry standard for low-latency audio; supports feedback (delay/chorus/reverb) naturally.
- **NVI (Non-Virtual Interface)**: Public `pull()` wraps profiling and calls protected `do_pull()`. Ensures performance tracking always runs and keeps the node contract consistent.
- **Vector-based**: `std::span<float>` for block I/O; block size is not hardcoded.
- **C interop**: Minimal, stable C API in `bridge/CInterface.h` for Swift, .NET, Linux GUIs.

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

---

## Key Design Decisions

- **Block size**: Configurable (not hardcoded); default 1024.
- **Format**: Float (-1..1) internally; convert at HAL boundary.
- **Buffer ownership**: Graph owns buffers; nodes use spans (zero-copy).
- **Polyphony**: Compile-time `AUDIO_MAX_VOICES` (e.g. 16); runtime cap optional.
- **Voice stealing**: Prefer releasing voice (longest first), else active (e.g. lowest note).
- **Per-voice params**: Velocity and aftertouch via **query pattern** (VoiceContext in `pull()`).
- **Wavetable**: Single `WavetableOscillatorProcessor` class; shape (Sine/Saw/Square/Triangle) selected at create; same C API as other oscillators.

---

## References

- Full historical plan (optional): `.cursor/plans/c++20_block-based_dsp_architecture_4b3bff39.plan.md`
- Project rules: repo root `.cursorrules` (branch naming, C++20/23, bin output, C interop).
