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
- **Mono-until-Stereo**: Keep signal paths mono for CPU efficiency until spatial effects or stereo-specific processing (panners/reverb) are required. All internal Voice components (VCO, VCF, VCA) must operate on a single mono `std::span<float>`.
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
│         │  Mono Graph  │  │  Mono Graph  │              │
│         └──────────────┘  └──────────────┘              │
│                       │                                 │
│         ┌─────────────▼──────────────┐                  │
│         │    Mono → Stereo Mixer     │                  │
│         │  (Panning & Voice Spread)  │                  │
│         └────────────────────────────┘                  │
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
| 12    | **MIDI Integration**: Creation of `MidiHAL` and `MidiParser` feeding into `VoiceManager`. | 100% Complete |
| 13    | **Golden Era Expansion**: SH-101 & Juno-60 building blocks (Sub-Osc, Source Mixer, Chorus, JSON Persistence). | In Progress |
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
- **Flexible Voice Topology (Instrument Templates)**: To support diverse synthesizer architectures (SH-101, Juno-60, MS-20, etc.) without fixed-function classes, the `Voice` is defined as a **Dynamic Node Container**:
    - **Signal Path (The Patch)**: Instead of hardcoded members, a `Voice` maintains a `std::vector<std::unique_ptr<Processor>> signal_chain_`. The `pull()` order defines the serial or parallel routing.
    - **Static Signal Path Rule**: To ensure RT-safety, the `signal_chain_` is "baked" during initialization/patching (outside the audio thread). Real-time re-patching (changing the vector size or order) is forbidden during the audio callback.
    - **Node Ownership & Execution**: The first node in the chain must be a **Generator** (SourceMixer or Oscillator) which clears the buffer. Subsequent nodes (Filters, Envelopes) are **Processors** that modify the buffer in-place.
    - **Source Mixer Node**: A standard `Processor` node that sums multiple generators (Pulse, Saw, Sub, Noise) before the filter stage. It applies `tanh` soft-saturation to emulate analog headroom.
    - **Buffer Reuse Strategy**: Use `borrow_buffer()` logic to provide temporary spans for parallel oscillators before they are summed in the `SourceMixer`.
    - **Factory-Based Configuration**: Specific instruments are created via `VoiceFactory::createSH101()` etc., which pre-configure the node chain and default modulation routings.
    - **Parameter Mapping Layer**: A mapping layer translates global Parameter IDs (e.g., "Cutoff") to the correct internal node and parameter based on the active template.
- **Exponential Parameter Scaling**: Pitch and Filter Cutoff modulation follow a logarithmic/octave-based response: $f_{final} = f_{base} \cdot 2^{mod}$, where $mod$ is the sum of modulation offsets in octaves.
- **Base + Offset Accumulation**: Processors maintain a "Base" value (anchor). Each block, the `ModulationMatrix` sums all offsets (bipolar) and applies them exponentially to the base.
- **Soft-Saturated Mixing (Phase 13)**: To emulate analog growl and headroom, the Source Mixer uses a `tanh` soft-saturation curve on the summed output. This prevents harsh digital clipping and provides harmonic richness when multiple oscillators are pushed into the filter.
- **Classic Polyphonic Signal Flow (Phase 13.5)**:
    - **The Mono Voice Primitive**: Each voice remains a strictly mono "black box" that handles its own internal modulation (LFO, Envelopes, CV). The terminal node (VCA) provides a mono signal to the mixer.
    - **The Mono → Stereo Mixer**: The first point in the graph where stereo `AudioBuffer` is used. It aggregates mono voice outputs, applies constant-power panning ($L = \cos(\theta)$, $R = \sin(\theta)$), and manages polyphonic spread (e.g., alternating voices across the stereo field).
- **Pull Protocol**:
    - **Node Classification**:
        - **Generators (Sources)**: e.g., `SineOscillatorProcessor`. These nodes assign values to the buffer, clearing any previous data. Logic: `buffer[i] = new_sample;`
        - **Processors (Modifiers)**: e.g., `AdsrEnvelopeProcessor`, Filters. These nodes must multiply or modify the buffer in-place to preserve the signal chain. Logic: `buffer[i] *= envelope_sample;` (VCA Behavior)
    - **Consistent Timebase**:
        - **Initialization**: Every node in the graph must be explicitly initialized with the hardware-verified sample rate of **48000Hz**.
        - **Requirement**: No node shall assume a default sample rate; accurate timing for ADSR ramps and oscillator frequencies depends on this alignment.

---

## The Voice-Manager Contract

For reliable polyphony and voice stealing, every `Voice` must adhere to the following contract:

- **is_active() Check**: A voice is considered "active" as long as its VCA envelope is in any stage other than IDLE or its output amplitude is above a noise floor threshold. The `VoiceManager` relies on this state to decide when a voice can be reassigned.
- **Lifecycle States**:
    - **Gate ON**: Triggers envelopes and resets oscillators (if sync is enabled).
    - **Gate OFF**: Transitions envelopes to the Release stage.
    - **Kill/Steal**: Forces an immediate (but smoothed) fade-out to reassign the voice hardware to a new note.

---

## Audio-Rate Modulation Matrix

- **Modulation as Audio**: All modulation signals (LFOs, Envelopes) are pulled at the same 48000Hz rate as the audio oscillators to ensure "zipper-free" parameter changes and FM/Filter-FM capabilities.
- **Span-Based Modulation**: To achieve true "zipper-free" performance, modulation nodes should provide a full span of modulation data per block. Processors use these spans to smoothly ramp internal parameters (e.g., Cutoff, Pitch) rather than updating once per block.
- **Target Update**: Modulation targets (Cutoff, Pitch) are recalculated per-block or per-sample using the exponential formula $f_{final} = f_{base} \cdot 2^{mod}$.

## Modular Modulation Matrix

The `ModulationMatrix` is a RT-safe central hub within each `Voice` that manages connections between sources and targets.

### Architecture
- **Bipolar Modulation**: Supports negative intensity for inverted envelopes (closing a filter) or phase-flipped LFOs.
- **Summing Logic**: Multiple sources targeting the same parameter are summed into a single modulation delta before being applied exponentially to the base parameter.
- **Encapsulation**: Owned by the `Voice`, keeping each instrument instance self-contained.

### Core Musical Mappings
- **Chiff**: Refactored as an `Envelope -> Filter Cutoff` modular route with positive intensity (e.g., +1.0 octaves).
- **90/10 Articulation**: Treated as a modular "Gate-to-Amplitude" link with a specific timing offset, defining the organ's detached feel as a patch setting.
- **Exponential Formula**: Both Pitch and Filter Cutoff follow $f_{final} = f_{base} \cdot 2^{mod}$ where $mod$ is the sum of modulation in octaves.

---

## References
- Project rules: repo root `.clinerules` (Git workflow, NVI, C++20).
