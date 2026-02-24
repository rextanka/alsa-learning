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
- **macOS**: Tahoe 26.3+ (CoreAudio)
- **Linux**: Fedora 42+ (ALSA)
- **Windows**: 11 (WASAPI)

### Technical Pillars
- **Pull-Based Heartbeat**: Sample-accurate timing driven by the `AudioDriver`. Output pulls from the graph; processors pull from their inputs.
- **Modular Routing Vision**: Moving from fixed-function blocks to a dynamic graph of:
    - **Sources**: Oscillators, Wavetables, File Players.
    - **Processors**: Filters, Envelopes, FX (Reverb, Delay).
    - **Sinks**: Audio Output (HAL), Non-Intrusive Loggers, Visualizers.
- **Mono-until-Stereo**: Keep signal paths mono for CPU efficiency until spatial effects or stereo-specific processing (panners/reverb) are required.
- **MPE & Microtonality**: Per-voice independence with a modular `TuningSystem` for cross-platform musical flexibility.
- **Centralized Zipper-Free Control**: A dedicated `ParameterManager` handles all ramping and smoothing "magic."
- **HAL-Only Interaction**: High-level DSP logic and tests interact ONLY with the `hal::AudioDriver` base class. Platform parity is maintained by swapping the HAL implementation (ALSA vs. CoreAudio) while keeping core C++ logic identical.

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

## File Structure (Finalized)

```
cxx/
├── include/
│   └── CInterface.h           # Public C API for Cross-Platform Interop
├── src/
│   ├── bridge/
│   │   └── AudioBridge.cpp    # C-to-C++ implementation (Bridge)
│   ├── core/
│   │   ├── Engine.hpp         # High-level engine (planned)
│   │   ├── VoiceManager.hpp   # Polyphony & Voice Stealing
│   │   ├── Voice.hpp          # Per-voice graph container
│   │   ├── MusicalClock.hpp   # Sample-accurate timing logic
│   │   ├── Logger.hpp         # RT-Safe Non-Intrusive Logging
│   │   └── AudioBuffer.hpp    # Multi-channel buffer handling
│   ├── dsp/
│   │   ├── Processor.hpp      # Base processor class (NVI Pattern)
│   │   ├── InputSource.hpp    # Pull interface
│   │   ├── oscillator/        # Sine, Saw, Pulse, Wavetable
│   │   ├── envelope/          # ADSR, AD
│   │   └── filter/            # Moog, Diode Ladder
│   └── hal/
│       ├── AudioDriver.hpp    # Cross-platform HAL interface
│       ├── alsa/              # Linux/Fedora implementation
│       └── coreaudio/         # macOS implementation
└── tests/
    ├── unit/                  # GTest-based C++ logic tests
    └── integration/           # Bridge and Hardware validation tests
```

---

## Cross-Platform Strategy

The project maintains a strict separation between **Platform HAL** and **Core DSP**. 
- **Fedora (ALSA)**: Primary development environment for high-priority RT-hardening.
- **macOS (CoreAudio)**: Target for Swift/Bridge interop and creative UI development.
- **Portability Guard**: The `CInterface` ensures that regardless of the underlying OS or HAL, the binary contract remains stable for host applications (Swift, .NET, or C++ GUIs).

---

## Migration Phases

| Phase | Description | Status |
|-------|-------------|--------|
| 1-11  | Core DSP, Factory, Polyphony, Musical Clock, Dual-Layer Testing | 100% Complete |
| 12    | **MIDI Integration**: Creation of `MidiHAL` and `MidiParser` feeding into `VoiceManager`. | Planned |
| 13    | **The Stereo Mixer & Bus**: Summing mono sources with panning to stereo output. | 100% Complete |
| 14    | **Spatial & Stereo FX**: Reverb, Chorus, Flanger, and Delay. | Planned |
| 15    | **Host Interrogation & Enumeration**: Safely query device list and hardware sample rates via UTF-8 C-Bridge. | Planned |
| 16    | **Non-Intrusive Logger**: RT-safe lock-free logging. | 100% Complete |
| 17    | **Unit & Integration Strategy**: GUnit vs. standalone API tests. | 100% Complete |
| 18    | **Optimization**: SIMD, fast-math, and dynamic 'Mono-to-Stereo' negotiation. | Planned |

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
