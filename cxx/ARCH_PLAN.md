# C++20 Block-Based DSP Architecture Plan

**Default plan for the `cxx/` engine.** This document is the single source of truth for architecture, contract, migration phases, and design decisions. Check it into git and keep it updated as we implement.

---

## Development Policy

To maintain the high reliability required for professional-grade audio software, the following policies are strictly enforced:

1.  **Green Build Requirement**: All existing tests must pass (`ctest` or `./bin/unit_tests`) before a Pull Request is created or merged.
2.  **Test-First Features**: Every new feature or architectural change MUST be accompanied by at least one unit test or integration test.
3.  **Regression Testing**: Every bug fix MUST include a corresponding regression test that fails without the fix and passes with it.
4.  **RT-Safe Documentation**: Any new code in the audio callback path must be documented as "RT-Safe" (no locks, no allocations, no `printf`).

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
│              (include/CInterface.h, bridge/AudioBridge.cpp) │
└──────────────────────┬──────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────┐
│              VoiceManager (Polyphony)                   │
│         ┌──────────────┐  ┌──────────────┐              │
│         │    Voice 1   │  │    Voice N   │              │
│         │  Graph      │  │  Graph       │              │
│         └──────────────┘  └──────────────┘              │
└──────────────────────┬──────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────┐
│              Processor base (dsp/Processor.hpp)         │
│  Oscillators │ Envelope │ Filter                         │
└──────────────────────┬──────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────┐
│              AudioDriver HAL (hal/AudioDriver.hpp)      │
│         ALSA │ CoreAudio │ WASAPI                       │
└─────────────────────────────────────────────────────────┘
```

---

## File Structure (New)

```
cxx/
├── include/
│   └── CInterface.h           # Public C API
├── src/
│   ├── bridge/
│   │   └── AudioBridge.cpp    # C-to-C++ implementation
│   ├── core/
│   │   ├── Engine.hpp         # High-level engine (planned)
│   │   ├── VoiceManager.hpp   # Polyphony & stealing
│   │   ├── Voice.hpp          # Per-voice graph
│   │   ├── MusicalClock.hpp   # Sample-accurate timing
│   │   └── AudioBuffer.hpp    # Multi-channel buffer handling
│   ├── dsp/
│   │   ├── Processor.hpp      # Base processor class
│   │   ├── InputSource.hpp    # Pull interface
│   │   ├── oscillator/        # Sine, Saw, Square, etc.
│   │   ├── envelope/          # ADSR, AD
│   │   └── filter/            # Moog, Diode
│   └── hal/
│       ├── AudioDriver.hpp    # HAL interface
│       ├── alsa/              # Linux implementation
│       └── coreaudio/         # macOS implementation
└── tests/
    ├── metronome_test.cpp     # Timing validation
    └── (GTest files...)       # Upcoming Phase 11
```

---

## Migration Phases

| Phase | Description | Status |
|-------|-------------|--------|
| 1-10  | Core DSP, Factory, Polyphony, Musical Clock, Metronome Validation | 100% Complete |
| 11    | **Dual-Layer Testing**: Establish GoogleTest (gtest) for internal C++ logic and C-API integration tests for the bridge. | 100% Complete |
| 12    | **MIDI Integration**: MIDI HAL for Linux/Mac/Win with CC mapping and SysEx hooks. | Planned |
| 13    | **Non-Intrusive Logger**: Implement a lock-free, real-time safe logger to replace `printf` in audio threads. | 100% Complete |
| 14    | **Unit & Integration Strategy**: Detail the plan for GUnit vs. standalone API tests. | 100% Complete |
| 15    | **Optimization**: SIMD, fast-math, and dynamic 'Mono-to-Stereo' negotiation to maximize polyphony. | Planned |

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
- Project rules: repo root `.clinerules` (Git workflow, NVI, C++20).
