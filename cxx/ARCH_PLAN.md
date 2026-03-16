# C++20 Block-Based DSP Architecture Plan

**Default plan for the `cxx/` engine.** This document is the single source of truth for architecture, contract, migration phases, and design decisions. Check it into git and keep it updated as we implement.

---

## Development Policy

To maintain the high reliability required for professional-grade audio software, the following policies are strictly enforced:

1.  **Git Policy**: Branch naming, commit standards, PR process, and documentation sync rules are defined in [cxx/docs/GIT_POLICY.md](cxx/docs/GIT_POLICY.md).
1.  **Test Source of Truth**: All testing standards, including the "Golden Lifecycle" and mandatory modular routing protocols, are defined in [cxx/docs/TESTING.md](cxx/docs/TESTING.md).
1.  **Green Build Requirement**: All existing tests must pass (`ctest` or `./bin/unit_tests`) before a Pull Request is created or merged.
2.  **Test-First Features**: Every new feature or architectural change MUST be accompanied by at least one unit test or integration test.
3.  **Regression Testing**: Every bug fix MUST include a corresponding regression test that fails without the fix and passes with it.
4.  **RT-Safe Documentation**: Any new code in the audio callback path must be documented as "RT-Safe" (no locks, no allocations, no `printf`).

---

## Design Manifesto

### Philosophy
Create a portable, lightweight **'Musical Toolbox'** for creative exploration and **'Sound Toy'** development. The library is designed to be approachable and educational, allowing developers to easily build small musical programs and understand core DSP concepts.

Target use cases include: signal generators, MIDI file players, and classic subtractive synthesizer architectures (SH-101, MS-20, TB-303, Juno-60 style).

The library is consumed via a stable C API, enabling host integration in native frameworks (Swift on macOS, Tauri/React on desktop) without exposing C++ internals.

### Target Platforms
- **macOS**: Tahoe 26.3+ (CoreAudio)
- **Linux**: Fedora 42+ (ALSA)
- **Windows**: 11 (WASAPI)

### Technical Pillars
- **Pull-Based Heartbeat**: Sample-accurate timing driven by the `AudioDriver`. Output pulls from the graph; processors pull from their inputs.
- **Modular Routing Vision**: Moving from fixed-function blocks to a dynamic graph of:
    - **Sources**: Oscillators, Wavetables, MIDI File Players.
    - **Processors**: Filters, Envelopes, FX (Reverb, Delay).
    - **Sinks**: Audio Output (HAL), Non-Intrusive Loggers, Visualizers.
    - All nodes declare typed ports (`PORT_AUDIO` or `PORT_CONTROL`). Both run at audio rate (`std::span<float>` per block). Connections are validated at `bake()` — `PORT_AUDIO` outputs may only connect to `PORT_AUDIO` inputs, and `PORT_CONTROL` outputs may only connect to `PORT_CONTROL` inputs. See [cxx/docs/MODULE_DESC.md](cxx/docs/MODULE_DESC.md) for per-module port specifications.
- **Mono-until-Stereo**: Keep signal paths mono for CPU efficiency until spatial effects or stereo-specific processing (panners/reverb) are required. All internal Voice components (VCO, VCF, VCA) must operate on a single mono `std::span<float>`.
- **Voice Groups**: Voices are partitioned into named groups, each with an independent signal chain topology and patch. This enables keyboard splits (e.g. below middle C → SH-101 group, above → Juno group) and layering (a single MIDI note triggers voices from multiple groups simultaneously).
- **Centralized Zipper-Free Control**: A dedicated `ParameterManager` will handle all parameter ramping and smoothing. See Phase 19 in the migration table.
- **HAL-Only Interaction**: High-level DSP logic and tests interact ONLY with the `hal::AudioDriver` base class. Platform parity is maintained by swapping the HAL implementation (ALSA vs. CoreAudio) while keeping core C++ logic identical.

### Future Roadmap (Not Currently Planned)
- **MPE & Microtonality**: Per-voice pitch independence with a modular `TuningSystem` for cross-platform musical flexibility. Deferred until core dynamic routing is stable.

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    C Interface Layer                     │
│              (include/CInterface.h, bridge/AudioBridge.cpp) │
└──────────────────────┬──────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────┐
│              VoiceManager (Polyphony + Voice Groups)    │
│  ┌──────────────────────┐  ┌──────────────────────┐     │
│  │    Voice Group A     │  │    Voice Group B     │     │
│  │    Voices 1-8        │  │    Voices 9-16       │     │
│  │  (e.g. SH-101 patch) │  │  (e.g. Juno patch)  │     │
│  └──────────┬───────────┘  └──────────┬───────────┘     │
│             │                         │                  │
│         ┌───▼─────────────────────────▼───┐              │
│         │       Mono → Stereo Mixer       │              │
│         │    (Panning & Voice Spread)     │              │
│         └─────────────────────────────────┘              │
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
│   │   ├── AudioBuffer.hpp    # Multi-channel buffer handling
│   │   ├── AudioGraph.hpp     # Dynamic routing graph
│   │   ├── Logger.hpp         # RT-Safe Non-Intrusive Logging
│   │   ├── MidiParser.hpp     # MIDI event handling
│   │   ├── ModulationMatrix.hpp # Bipolar signal routing
│   │   ├── MusicalClock.hpp   # Sample-accurate timing logic
│   │   ├── PatchStore.hpp     # JSON patch management
│   │   ├── Voice.hpp          # Per-voice graph container
│   │   └── VoiceManager.hpp   # Polyphony & Voice Stealing
│   ├── dsp/
│   │   ├── Processor.hpp      # Base processor class (NVI Pattern)
│   │   ├── InputSource.hpp    # Pull interface
│   │   ├── envelope/          # ADSR, AD, Envelope base
│   │   ├── filter/            # Moog, Diode Ladder
│   │   ├── fx/                # Juno Chorus, Delay
│   │   ├── oscillator/        # Sine, Saw, Pulse, Sub, LFO
│   │   └── routing/           # SourceMixer, SummingMixer
│   └── hal/
│       ├── AudioDriver.hpp    # Cross-platform HAL interface
│       ├── alsa/              # Linux/Fedora implementation
│       └── coreaudio/         # macOS implementation
└── tests/
    ├── functional/            # Scenario-based engine tests
    ├── integration/           # Bridge and Hardware validation tests
    └── unit/                  # GTest-based component tests
```

---

## Cross-Platform Strategy

The project maintains a strict separation between **Platform HAL** and **Core DSP**.
- **Fedora (ALSA)**: Primary development environment for high-priority RT-hardening.
- **macOS (CoreAudio)**: Target for Swift/Bridge interop and creative UI development.
- **Tauri/React**: Desktop UI target consuming the C API via Tauri's native bridge. No WebAssembly compilation required; the native HAL (ALSA or CoreAudio) is used directly.
- **Portability Guard**: The `CInterface` ensures that regardless of the underlying OS or HAL, the binary contract remains stable for host applications (Swift, Tauri/React, or C++ GUIs).

---

## Audio Terminology

Precise use of these terms is mandatory throughout all source code, documentation, and API contracts. Ambiguity between "sample" and "frame" is a common source of buffer-sizing bugs.

### Sample
A **sample** is a single scalar quantised amplitude value for **one channel** at one point in time. It is the atomic unit of digital audio. Represented as `float` internally.

### Sample Frame
A **sample frame** (or simply **frame**) is the set of samples — one per channel — that are presented to the DAC simultaneously. The channel count determines the frame width:

| Stream Format | Samples per Frame | Layout |
|---|---|---|
| Mono | 1 | `[C]` |
| Stereo | 2 | `[L, R]` (interleaved) |
| Quadraphonic | 4 | `[FL, FR, RL, RR]` (interleaved) |

A buffer of *N* frames in a stereo stream therefore contains *2N* samples stored as `[L₀, R₀, L₁, R₁, …, L_{N-1}, R_{N-1}]`.

### Block
A **block** (or **processing block**) is a contiguous sequence of *N* frames processed atomically by the DSP graph in a single callback invocation. *N* is the **block size** (see Block Size Policy). The engine's internal voice graph and modulation matrix advance by exactly one block per callback.

### API Contract for `engine_process`
`engine_process(handle, float* output, size_t frames)` — the `frames` parameter is a **frame count**, not a sample count. The function produces **stereo interleaved** output via the same `SummingBus` path as the HAL callback, including voice panning and global FX (Chorus). The caller must provide a buffer of at least `frames × 2` floats.

```c
const size_t FRAMES = 512;
float output[FRAMES * 2]; // stereo interleaved: 1024 floats
engine_process(handle, output, FRAMES);
// output[i*2]   = left  sample of frame i
// output[i*2+1] = right sample of frame i
```

This guarantees that offline test analysis and live HAL rendering are driven by identical signal graph paths.

---

## Block Size Policy

Block size is **runtime-configurable** and must be chosen by the host at engine creation time based on hardware capability queries. There is no hardcoded default — the appropriate size is platform and hardware dependent (e.g. 512 samples on a modern ARM laptop for minimum latency; 1024 samples on older x86 hardware for stability).

- Query available block sizes via `host_get_device_block_sizes()` (Phase 17).
- Pass the chosen size to `engine_create(sample_rate, block_size)`.
- All internal buffers are allocated at engine creation time to accommodate the chosen size.
- `MAX_BLOCK_SIZE` in `Voice.hpp` defines the compile-time upper bound for stack buffer allocation.

---

## Migration Phases

| Phase | Description | Status |
|-------|-------------|--------|
| 1-11  | Core DSP, Factory, Polyphony, Musical Clock, Dual-Layer Testing | Complete |
| 12    | **MIDI Integration**: `MidiParser` and `MidiEvent` feeding into `VoiceManager`. | Complete |
| 13    | **Golden Era Expansion**: SH-101 & Juno-60 building blocks (Sub-Osc, Source Mixer, Chorus, JSON Persistence). | Complete |
| 14    | **Dynamic Signal Chain**: (a) Extend `Processor` with typed port declarations (`PORT_AUDIO`/`PORT_CONTROL`) and per-port tagging. (b) Separate `AdsrEnvelopeProcessor` from VCA into distinct nodes. (c) Refactor `Voice` to `signal_chain_` vector with `add_processor()`, named port connections, and `bake()` validation. (d) Implement `VoiceFactory` with correct topologies. (e) Add Voice Groups to `VoiceManager`. | In Progress |
| 15    | **Spatial & Stereo FX**: Reverb, Chorus, Flanger, and Delay. | Planned |
| 16    | **Host Interrogation & Enumeration**: Safely query device list, hardware sample rates, and supported block sizes via UTF-8 C-Bridge. | Planned |
| 17    | **Non-Intrusive Logger**: RT-safe lock-free logging. | Complete |
| 18    | **Unit & Integration Strategy**: GUnit vs. standalone API tests. | Complete |
| 19    | **ParameterManager**: Centralized zipper-free parameter ramping and smoothing for all audio-rate parameter changes. | Planned |
| 20    | **MIDI File Player**: `MidiFilePlayer` Source module for sequenced playback. | Planned |
| 21    | **Optimization**: SIMD, fast-math, and dynamic 'Mono-to-Stereo' negotiation. | Planned |

---

## Memory & Alignment

To achieve the **10ms MMA Latency Target** and ensure **RT-Safety**, the engine enforces strict buffer alignment rules:

### Power-of-Two Requirement
All internal DSP buffers, delay lines, and analysis windows (including `AudioTap` and `DctProcessor`) **MUST** utilize a power-of-two size (e.g., 512, 1024, 8192, 16384, 32768).

* **Performance Logic**: Power-of-two sizes allow the use of bitwise masking for index wrapping (`index & (size - 1)`) instead of the modulo operator (`index % size`).
* **RT-Safety**: Removing division/modulo instructions from the inner audio loop reduces CPU jitter and ensures deterministic execution.
* **Math Readiness**: This alignment is mandatory for future $O(N \log N)$ Fast Fourier Transform (FFT) optimizations and high-resolution spectral analysis.

### Spectral Analysis Resolution
To achieve sub-Hertz accuracy (±0.5% tolerance) at low frequencies (e.g., 110Hz), the engine utilizes **Zero-Padding** for spectral analysis.
* **Standard**: 16,384 samples of windowed audio are padded to 32,768 before DCT-II processing.
* **Justification**: This artificially increases bin density, providing the **Parabolic Interpolation** algorithm with the resolution required to overcome DC-offset bias and spectral leakage in the lower octaves.

### Implementation Standard
When implementing circular buffers or taps:
```cpp
// Correct RT-Safe wrapping
m_write_index = (m_write_index + 1) & (m_buffer_size - 1);
```

## Key Design Decisions

- **Block size**: Runtime-configurable; chosen by the host at engine creation time based on hardware capability queries. No hardcoded default. See Block Size Policy section below.
- **Format**: Float (-1..1) internally; convert at HAL boundary.
- **Buffer ownership**: Graph owns buffers; nodes use spans (zero-copy).
- **Polyphony**: Compile-time `AUDIO_MAX_VOICES` (e.g. 16); runtime cap optional.
- **Voice Groups**: Voices are partitioned into named groups, each with an independent `VoiceFactory`-configured topology. Enables keyboard splits and layering via the C API.
- **Voice stealing**: Prefer releasing voice (longest first), else active (e.g. lowest note). Applied within a voice group.
- **Per-voice params**: Velocity and aftertouch via **query pattern** (VoiceContext in `pull()`).
- **Wavetable**: Single `WavetableOscillatorProcessor` class; shape selected at create.
- **10ms MMA Latency Target**: Optimize buffer sizes and processing for consistent real-time response.
- **Nord-style Modular Routing**: Supporting "Audio-as-Control" where audio signals can be used as modulation sources across the graph.
    - **Flexible Voice Topology (Dynamic Configuration API)**: To support diverse synthesizer architectures (SH-101, MS-20, TB-303, etc.) without fixed-function classes, the `Voice` is defined as a **Dynamic Node Container**. This API enables shifting from a hardcoded synthesizer to a programmable DSP engine.
        - **The Great Deletion (Abolishing Hardcoded Members)**: The `Voice` class shall no longer contain hardcoded processor members (e.g., `pulse_osc_`, `moog_filter_`). All DSP logic resides within the `signal_chain_`.
        - **Dynamic Signal Chain**: A `Voice` maintains a `std::vector<std::unique_ptr<Processor>> signal_chain_` as its execution backbone. The `do_pull()` method iterates through this vector, executing nodes in sequence.
        - **API-First Construction**: The `Voice` exposes an API (e.g., `add_processor(std::unique_ptr<Processor> p, std::string tag)`) that allows tests, factory methods, and UI controllers to build complex signal chains at runtime.
        - **The "Generator-First" Rule (Topological Validation)**:
            - **Rule**: Every `signal_chain_` must begin with a **Generator** node (Oscillator or SourceMixer).
            - **Reason**: The first node is responsible for clearing the buffer (initialization); subsequent nodes are Processors that perform in-place modification.
            - **Validation**: A `bake()` call must be made after chain construction. `bake()` verifies that `signal_chain_[0]` is a Generator before the voice becomes active.
        - **Node Tagging & Discovery**:
            - **Tagging**: Each node in the chain can be assigned a unique string tag (e.g., "Filter_HP", "SubOsc").
            - **Discovery**: The `Voice` implements a discovery method. When a user sends a parameter update (e.g., "Cutoff"), the `Voice` uses the tag or processor type to find the target node(s) in its current chain.
        - **RT-Safety Guardrail**: Structural changes to the `signal_chain_` (adding/removing nodes) must only occur when the voice is **Idle** or via a thread-safe command queue. The `std::vector` must never be re-allocated while the audio thread is calling `pull()`.
        - **Lifecycle Delegation**:
            - **Contract**: The `Voice` is considered active if any `EnvelopeProcessor` node in its chain is in a non-idle state.
            - **Implementation**: `Voice::is_active()` delegates to its internal dynamic nodes, fulfilling the Voice-Manager Contract for polyphonic stealing without external synchronization risks.
        - **Strategic Versatility**: This API allows the construction of complex dual-filter architectures (MS-20 style) or minimal single-oscillator chains (SH-101 style) without changing a single line of `Voice` class code.
        - **Buffer Reuse Strategy**: Use `borrow_buffer()` logic to provide temporary spans for parallel oscillators before they are summed in the `SourceMixer`.
        - **Factory-Based Configuration**: Instrument chains are defined in patch files (v2 JSON) and constructed at runtime via `engine_add_module` / `engine_connect_ports` / `engine_bake`. `VoiceFactory` is retired in Phase 15 — chain construction is exclusively driven by the C API and patch loader.
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
    - **Initialization**: Every node in the graph must be explicitly initialized with the hardware-verified sample rate provided by the `AudioDriver`.
    - **Requirement**: No node shall assume a default sample rate; accurate timing for ADSR ramps and oscillator frequencies depends on this alignment.
    - **Integrity Rule**: Never use hard-coded sample rate constants (e.g., 44100, 48000) for internal logic or logging. Always obtain the current rate from the hardware configuration during engine setup.

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

## V. Parameter Export and Client Interface

To bridge the gap between the C-compatible public API and the internal Flexible Topology, the engine implements a **Tag-based Parameter Mapping protocol**.

### The Mapping Contract
- **Discovery Strategy**: Client labels (strings) are mapped to **Global Parameter IDs** (enums). Both forms are accepted by the C API and resolve through the same registry.
- **Resolution**: The `VoiceManager` and `Voice` resolve these Global IDs using a lookup table that maps to a `{Node Tag, Internal Node ID}` pair.
- **Node Tags**: Standardized tags ensure consistent targeting across different voice topologies.
  - `"VCO"`: Primary Oscillators (Sine, Saw, Pulse, Sub).
  - `"VCF"`: Filter section.
  - `"VCA"`: Amplifier and main amplitude envelope.

### Global Parameter Registry
| Global ID | String Label | Target Tag | Description | Range |
|-----------|--------------|------------|-------------|-------|
| 1 | `vcf_cutoff` | `VCF` | Filter cutoff frequency | Log (20Hz–20kHz) |
| 2 | `vcf_res` | `VCF` | Filter resonance | 0.0–1.0 |
| 3 | `vcf_env_amount` | `VCF` | Envelope to Filter modulation depth | Bipolar |
| 4 | `amp_attack` | `VCA` | VCA Envelope Attack | Time (s) |
| 5 | `amp_decay` | `VCA` | VCA Envelope Decay | Time (s) |
| 6 | `amp_sustain` | `VCA` | VCA Envelope Sustain | 0.0–1.0 |
| 7 | `amp_release` | `VCA` | VCA Envelope Release | Time (s) |
| 10 | `osc_pw` | `VCO` | Pulse Width (legacy alias for `pulse_width`) | 0.0–0.5 |
| 11 | `sub_gain` | `VCO` | Sub-oscillator level | 0.0–1.0 |
| 12 | `saw_gain` | `VCO` | Sawtooth level | 0.0–1.0 |
| 13 | `pulse_gain` | `VCO` | Pulse level | 0.0–1.0 |
| 14 | `pulse_width` | `VCO` | Pulse Width (native) | 0.0–0.5 |
| 15 | `noise_gain` | `VCO` | White noise level | 0.0–1.0 |

### Future Architectural Roadmap

1. **Global Modulation Bus**: Shift from per-voice matrices to a centralized bus where global sources (e.g., Vibrato LFO) write to synchronized slots, improving phase coherency and reducing CPU overhead via a subscription model.
2. **BaseOscillator Hierarchy**: Consolidate redundant logic (gain, fine-tuning, pitch-bend) into a polymorphic collection, allowing the `Voice` to manage generators as a dynamic list rather than hardcoded pointers.

### Implementation Rules
1. **Test Source of Truth**: All testing standards, including the "Golden Lifecycle" and mandatory modular routing protocols, are defined in [cxx/docs/TESTING.md](cxx/docs/TESTING.md).
1. **Bridge Duty**: The `AudioBridge` (EngineHandleImpl) MUST maintain a `param_name_to_id` map matching the "String Label" column.
2. **Voice Duty**: The `Voice` constructor MUST use `register_parameter(GlobalID, Tag, InternalID)` to link the bridge to the signal chain.
3. **RT-Safety**: All mapping lookups occur outside the audio thread or via lock-free atomic caches.

---
## Documentation Map
- **Build/CI**: BUILD.md
- **Git Policy**: cxx/docs/GIT_POLICY.md
- **Functional Testing**: cxx/docs/TESTING.md
- **C-Bridge Contract**: cxx/docs/BRIDGE_GUIDE.md
- **Processor Specifications**: cxx/docs/MODULE_DESC.md
- **Patch Format**: cxx/docs/PATCH_SPEC.md

## References
- Git workflow: [cxx/docs/GIT_POLICY.md](cxx/docs/GIT_POLICY.md) (branch naming, PR process, commit standards).
- Coding rules: repo root `.clinerules` (quick-reference; points to canonical docs above).


