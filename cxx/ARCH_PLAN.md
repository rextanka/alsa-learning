# C++20 Block-Based DSP Architecture Plan

**Default plan for the `cxx/` engine.** This document is the single source of truth for architecture, contract, migration phases, and design decisions. Check it into git and keep it updated as we implement.

---

## Development Policy

> **For AI agents**: When context is refreshed or a new session begins, read this file in full, then read each document listed in the **Documentation Map** (bottom of this file). All rules from all documents are active simultaneously. Do not proceed with any implementation until you have read the relevant governing documents.

To maintain the high reliability required for professional-grade audio software, the following policies are strictly enforced:

1.  **Git Policy**: Branch naming (timestamp prefix required; optional descriptive suffix), commit standards, PR process, and documentation sync rules are defined in [docs/GIT_POLICY.md](docs/GIT_POLICY.md). **Violating branch naming policy is not acceptable** — always check GIT_POLICY.md before creating a branch.
1.  **Test Source of Truth**: All testing standards, including the "Golden Lifecycle" and mandatory modular routing protocols, are defined in [docs/TESTING.md](docs/TESTING.md).
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
- **macOS**: Tahoe 26.3+ (CoreAudio, CoreMIDI)
- **Linux**: Fedora 42+ (ALSA, ALSA rawmidi)
- **Embedded Linux**: Raspberry Pi 4/5 running Raspberry Pi OS or similar lightweight Linux — Phases 25–26 target this explicitly

### Technical Pillars
- **Pull-Based Heartbeat**: Sample-accurate timing driven by the `AudioDriver`. Output pulls from the graph; processors pull from their inputs.
- **Modular Routing Vision**: Moving from fixed-function blocks to a dynamic graph of:
    - **Sources**: Oscillators, Wavetables, MIDI File Players.
    - **Processors**: Filters, Envelopes, FX (Reverb, Delay).
    - **Sinks**: Audio Output (HAL), Non-Intrusive Loggers, Visualizers.
    - All nodes declare typed ports (`PORT_AUDIO` or `PORT_CONTROL`). Both run at audio rate (`std::span<float>` per block). Connections are validated at `bake()` — `PORT_AUDIO` outputs may only connect to `PORT_AUDIO` inputs, and `PORT_CONTROL` outputs may only connect to `PORT_CONTROL` inputs. See [cxx/docs/MODULE_DESC.md](cxx/docs/MODULE_DESC.md) for per-module port specifications.
- **Mono-until-Stereo**: Keep signal paths mono for CPU efficiency until spatial effects or stereo-specific processing (panners/reverb) are required. All internal Voice components (VCO, VCF, VCA) must operate on a single mono `std::span<float>`.
- **Voice Groups**: Voices are partitioned into named groups, each with an independent signal chain topology and patch. This enables keyboard splits (e.g. below middle C → SH-101 group, above → Juno group) and layering (a single MIDI note triggers voices from multiple groups simultaneously).
- **Centralized Zipper-Free Control**: `SmoothedParam` (Phase 21) handles per-processor parameter ramping and smoothing. Each opted-in processor owns `SmoothedParam` member variables. Linear interpolation runs at block boundaries with a 10ms default ramp. Discrete/patch-config parameters use snap mode (`current = target` immediately). See `src/dsp/SmoothedParam.hpp` and Phase 21 in the migration table.
- **HAL-Only Interaction**: High-level DSP logic and tests interact ONLY with the `hal::AudioDriver` base class. Platform parity is maintained by swapping the HAL implementation (ALSA vs. CoreAudio) while keeping core C++ logic identical.

### Future Roadmap (Not Currently Planned)
- **Windows (WASAPI)**: Windows 11 via WASAPI HAL. No implementation exists yet; deferred until macOS and Linux HALs are fully hardened.
- **MPE & Microtonality**: Per-voice pitch independence with a modular `TuningSystem` for cross-platform musical flexibility. Deferred until core dynamic routing is stable.
- **WebAssembly**: Emscripten target consuming a Web Audio API driver. Deferred — requires its own HAL implementation and a sandboxed MIDI path.

---

## Architecture

```
┌───────────────────────────────────────────────────────────────────┐
│                         C Interface Layer                          │
│           (include/CInterface.h, bridge/AudioBridge.cpp)           │
└────────────────┬──────────────────────────────────┬───────────────┘
                 │                                  │
┌────────────────▼──────────────────────┐  ┌────────▼───────────────┐
│   VoiceManager (Polyphony + Groups)   │  │  MidiDriver HAL        │
│  ┌──────────────┐  ┌──────────────┐  │  │  (hal/MidiDriver.hpp)  │
│  │  Voice Grp A │  │  Voice Grp B │  │  │  ALSA rawmidi │ CoreMIDI│
│  │  Voices 1-8  │  │  Voices 9-16 │  │  └────────┬───────────────┘
│  └──────┬───────┘  └──────┬───────┘  │           │ MIDI events
│         │                 │          │           ▼ engine_process_midi_bytes
│     ┌───▼─────────────────▼───┐      │
│     │   Mono → Stereo Mixer   │      │
│     └─────────────────────────┘      │
└────────────────┬──────────────────────┘
                 │
┌────────────────▼──────────────────────────────────────────────────┐
│              Processor base (dsp/Processor.hpp)                    │
│  Oscillators │ Envelope │ Filter │ FX  [conditionally compiled]    │
└────────────────┬──────────────────────────────────────────────────┘
                 │
┌────────────────▼──────────────────────────────────────────────────┐
│              AudioDriver HAL (hal/AudioDriver.hpp)                 │
│         ALSA │ CoreAudio │ (WASAPI — future)                       │
└───────────────────────────────────────────────────────────────────┘
```

---

## File Structure (Finalized)

```
cxx/
├── include/
│   └── CInterface.h               # Public C API for Cross-Platform Interop
├── src/
│   ├── bridge/
│   │   └── AudioBridge.cpp        # C-to-C++ implementation (Bridge)
│   ├── core/
│   │   ├── AudioBuffer.hpp        # Multi-channel buffer handling
│   │   ├── AudioGraph.hpp         # Dynamic routing graph
│   │   ├── Logger.hpp             # RT-Safe Non-Intrusive Logging
│   │   ├── MidiParser.hpp         # MIDI event handling
│   │   ├── MidiFilePlayer.hpp     # SMF playback engine (Phase 22A)
│   │   ├── MusicalClock.hpp       # Sample-accurate timing logic
│   │   ├── ModuleRegistry.hpp     # Self-registering processor registry
│   │   ├── PatchStore.hpp         # JSON patch management
│   │   ├── ProcessorRegistrations.cpp  # Central registration (conditionally replaced by Phase 26)
│   │   ├── SmfParser.hpp          # Standard MIDI File parser (Phase 22A)
│   │   ├── Voice.hpp              # Per-voice graph container
│   │   └── VoiceManager.hpp       # Polyphony & Voice Stealing
│   ├── dsp/
│   │   ├── Processor.hpp          # Base processor class (NVI Pattern)
│   │   ├── InputSource.hpp        # Pull interface
│   │   ├── SmoothedParam.hpp      # Per-parameter linear-ramp interpolation (Phase 21)
│   │   ├── envelope/              # ADSR, AD, Envelope base
│   │   ├── filter/                # Moog, Diode, SH, MS20, HP, BP ladders; VcfBase
│   │   ├── fx/                    # Juno Chorus, Echo Delay, Freeverb, FDN Reverb, Phaser, Distortion
│   │   ├── oscillator/            # Sine, Saw, Pulse, Sub, LFO, White/Pink Noise
│   │   └── routing/               # CompositeGenerator, DrawbarOrgan, CV/Audio utilities
│   └── hal/
│       ├── AudioDriver.hpp        # Cross-platform audio HAL interface
│       ├── HostDeviceInfo.hpp     # Platform-agnostic audio device descriptor
│       ├── MidiDriver.hpp         # Cross-platform MIDI HAL interface (Phase 25)
│       ├── HostMidiDeviceInfo.hpp # Platform-agnostic MIDI device descriptor (Phase 25)
│       ├── alsa/                  # Linux audio + MIDI implementation
│       │   ├── AlsaDriver.hpp/.cpp
│       │   └── AlsaMidiDriver.hpp/.cpp   # Phase 25
│       └── coreaudio/             # macOS audio + MIDI implementation
│           ├── CoreAudioDriver.hpp/.cpp
│           └── CoreMidiDriver.hpp/.cpp   # Phase 25
├── tools/
│   └── configure_modules.py       # Module configuration tool for embedded targets (Phase 26)
└── tests/
    ├── functional/                # Scenario-based engine tests
    ├── integration/               # Bridge and Hardware validation tests
    └── unit/                      # GTest-based component tests
```

---

## Cross-Platform Strategy

The project maintains a strict separation between **Platform HAL** and **Core DSP**.
- **Fedora (ALSA)**: Primary development environment for high-priority RT-hardening.
- **macOS (CoreAudio)**: Target for Swift/Bridge interop and creative UI development.
- **Tauri/React**: Desktop UI target consuming the C API via Tauri's native bridge. No WebAssembly compilation required; the native HAL (ALSA or CoreAudio) is used directly.
- **Windows (WASAPI)**: Deferred — see Future Roadmap.
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

> **Internal vs external format**: Internally, `AudioBuffer` stores channels as separate non-interleaved `std::vector<float>` planes (`channel[0]` = L, `channel[1]` = R). The interleaving into the caller's `float*` output buffer happens at the `AudioBridge` boundary inside `engine_process`. Tests and internal DSP code should never assume a flat interleaved layout when working with `AudioBuffer` objects directly.

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

- Query available block sizes via `host_get_device_block_size(device_index)` — implemented in Phase 20.
- Pass the chosen size to `engine_create(sample_rate)`.
- Tests must use `test::get_safe_sample_rate()` and never hardcode block sizes. See TESTING.md §8.

---

## Sample Rate Policy

The engine supports **44100 Hz and 48000 Hz** exclusively. These are the two rates common to consumer and professional computer audio interfaces, including USB interfaces.

- **Rate negotiation**: The caller is responsible for querying `host_get_device_sample_rate(device_index)` (Phase 20 — probes actual hardware via CoreAudio/ALSA) and passing the result to `engine_create(sample_rate)`. The engine accepts whatever rate is passed; enforcing the >48kHz cap is the caller's responsibility.
- **No hardcoded rates**: All internal timing — ADSR curves, LFO rates, delay times, metronome — must derive from the runtime `sample_rate_` stored at engine creation. No source file may contain the literals `44100` or `48000` for timing logic.
- **USB interfaces**: USB audio interfaces may report stricter latency constraints and non-standard preferred block sizes. Block size must come from `host_get_device_block_size()` (Phase 20), not assumed.
- **Processing budget**: Rates above 48000 Hz double or quadruple processing cost for 16-voice polyphony. 48000 Hz is the practical ceiling for this engine's target hardware.

---

## Migration Phases

| Phase | Description | Status |
|-------|-------------|--------|
| 1-11  | Core DSP, Factory, Polyphony, Musical Clock, Dual-Layer Testing | Complete |
| 12    | **MIDI Integration**: `MidiParser` and `MidiEvent` feeding into `VoiceManager`. | Complete |
| 13    | **Golden Era Expansion**: SH-101 & Juno-60 building blocks (Sub-Osc, Source Mixer, Chorus, JSON Patch Loading). | Complete |
| 14    | **Dynamic Signal Chain**: (a) `Processor` typed port declarations (`PORT_AUDIO`/`PORT_CONTROL`) and `input_port_type()`. (b) Separate `AdsrEnvelopeProcessor` from VCA into distinct nodes. (c) `Voice` `signal_chain_` vector with `add_processor()`, `bake()` Generator-First and port-type validation. (d) Voice Groups in `VoiceManager`. | Complete — all items complete; (c) validator relaxed in Phase 16 to allow PORT_CONTROL nodes before the first PORT_AUDIO generator (enables chain-leading LFO) |
| 15    | **Module Registry, Named Port Connections & Patch v2**: (a) `ModuleRegistry` — self-registration via static initializers, queryable via C API. (b) `declare_port()` / `declare_parameter()` on `Processor`. (c) Explicit `PortConnection` graph in `Voice`; `bake()` validates named connections. (d) Data-driven `pull_mono` — full graph executor (audio + CV, multi-input, feedback). (e) Patch format v2 — single-group JSON with named connections. (f) `engine_add_module` / `engine_connect_ports` / `engine_bake` / `engine_load_patch` C API. (g) `engine_set_modulation` removed; `VoiceFactory` retired. (h) `DrawbarOrganProcessor` generator module. (i) Four reference patches: `sh_bass`, `tb_bass`, `organ_drawbar`, `juno_pad`. (j) True stereo `engine_process` via `SummingBus`/`AudioBuffer` path. (k) Full functional test migration to Phase 15 chain. (l) Phase 15A: `engine_set_lfo_*` C API exposing internal Voice LFO + `ModulationMatrix`; external integer-ID mod matrix deprecated. | Complete |
| 16    | **Full Control-Rate Port Routing**: Retire the deprecated integer-ID external modulation matrix entirely. Extend the `Voice::pull_mono` graph executor so that every `PORT_CONTROL` connection declared via `engine_connect_ports` is live-routed at block rate — not just `gain_cv`→VCA. Concrete targets: (a) Any `PORT_CONTROL` source (LFO, ENV) wired via named port to `pitch_cv`, `pwm_cv`, or `cutoff_cv` is applied each block. (b) `Voice::lfo_` and `ModulationMatrix` retired; all modulation routes are first-class named port connections. (c) `engine_create_processor`, `engine_connect_mod`, `engine_get_modulation_report`, `MOD_SRC_*`/`MOD_TGT_*`/`ALL_VOICES` removed from `CInterface.h`. (d) `engine_set_lfo_*` / `engine_clear_modulations` removed from bridge and header. (e) VoiceManager global mod matrix (`connections_`, `mod_sources_`, `mod_buffer_`) retired. (f) LFO is a first-class chain module: added via `engine_add_module("LFO", tag)` + `engine_connect_ports`; `juno_pad.json` patch updated. (g) `bake()` validator relaxed — PORT_CONTROL nodes may precede first PORT_AUDIO generator. | Complete |
| 17    | **CV & Modulation Utilities + Full Patch Test Suite**: New routing processors (CV_MIXER, CV_SPLITTER, MATHS, GATE_DELAY, SAMPLE_HOLD, INVERTER); `InverterProcessor` fix; `VcfBase` + `LadderVcfBase` extraction; `kybd_cv` hot-path cache; `spectral_centroid()` analysis API; 12-patch × dedicated-test functional suite (40 tests). | Complete |
| 18    | **Audio Routing & Timbral Completion**: Multi-input graph executor; `Processor::inject_audio()`; RING_MOD, AUDIO_SPLITTER, HIGH_PASS_FILTER, BAND_PASS_FILTER, NOISE_GATE, ENVELOPE_FOLLOWER; VCO `fm_in` / `fm_depth`; Filter audio-rate FM; Pink noise (Paul Kellett 7-pole IIR); `bell.json` patch. Module registry grows to 24 types. | Complete |
| 19    | **Spatial & Stereo FX**: Global post-summing bus in `VoiceManager`; REVERB_FREEVERB (Schroeder/Freeverb, 8 comb + 4 allpass); REVERB_FDN (Jot FDN, 8-line Householder, exact T60); PHASER (4/8-stage all-pass, LFO-swept); `engine_post_chain_push` / `engine_post_chain_set_param` / `engine_post_chain_clear` C API. | Complete |
| 20    | **Host Interrogation & Enumeration**: `HostDeviceInfo` struct; `AudioDriver::enumerate_devices()` (CoreAudio + ALSA); `device_name()` virtual; 9 new C-Bridge functions (`host_get_device_*`, `engine_get_driver_*`). HAL is sole source of truth — zero platform `#ifdef`s in Bridge. | Complete |
| 21    | **ParameterManager (SmoothedParam)**: Per-processor `SmoothedParam` members for all audio-rate continuous parameters. Linear interpolation at block boundaries (10ms default ramp). Snap mode for discrete selectors and patch-config values. `Voice::pan_param_` for click-free pan transitions. `ADEnvelopeProcessor` fully declared (ports, parameters, `apply_parameter`, `on_note_on`). | Complete |
| 22    | **SMF Playback — Phase A (Rudimentary)**: `SmfParser` + `MidiFilePlayer` — sample-accurate SMF Format 0/1 playback. All tracks merged; MIDI channel ignored (channel-blind). All voices use the single loaded patch. `engine_load_midi` / `engine_midi_play` / `engine_midi_stop` / `engine_midi_rewind` / `engine_midi_get_position` C API. Tempo map support (FF 51). Note On velocity-0 normalised to Note Off. Running status. SysEx skipped. | Complete |
| 23    | **SMF Playback — Phase B (Multi-timbral)**: Extend `MidiFilePlayer` with a MIDI-channel-to-VoiceGroup routing table. Each channel maps to a VoiceGroup with its own patch. `engine_midi_set_channel_patch(ch, patch_path)` C API. Requires Phase 16 VoiceGroup/patch-per-group support. Program Change events (`0xC0`) trigger live patch swaps on the target channel's group. | Planned |
| 24    | **Optimization**: SIMD, fast-math, and dynamic 'Mono-to-Stereo' negotiation. | Planned |
| 25    | **USB MIDI HAL**: Platform-agnostic `hal::MidiDriver` base with static factory pattern (matching `AudioDriver`). Platform drivers: `AlsaMidiDriver` (ALSA rawmidi) and `CoreMidiDriver` (CoreMIDI framework). MIDI input dispatches to the engine via `engine_process_midi_bytes`. MIDI output sends raw bytes to a connected device. `HostMidiDeviceInfo` struct for enumeration. New C API: `midi_*` family. No platform `#ifdef`s in bridge layer. | Planned |
| 26    | **Static Module Configuration**: Decouple module registration from the central `ProcessorRegistrations.cpp` into per-module self-registration (static initializers in each processor's `.cpp`). CMake `option()` flags (`AUDIO_MODULE_REVERB`, `AUDIO_MODULE_FX`, etc.) control which `.cpp` files compile, stripping unused modules from the binary. Named CMake presets for common targets (`desktop_full`, `pi_minimal`, `pi_synth`). Python tool `tools/configure_modules.py` for interactive preset configuration. Enables the engine to run on Raspberry Pi with a binary footprint 50–70% smaller than the full desktop build. | Planned |

---

## MIDI Architecture

The engine supports two independent MIDI input paths:

### Path 1 — Real-Time Byte Stream (Phase 12)

External MIDI hardware or host DAW events arrive as raw byte streams via `engine_process_midi_bytes(handle, data, size, sampleOffset)`. The `MidiParser` state machine reconstructs messages (including Running Status) and dispatches `MidiEvent` structs to `VoiceManager`. The `sampleOffset` field places each event at a precise sample position within the current audio block.

### Path 2 — SMF File Playback (Phase 22A/22B)

`SmfParser::load()` pre-parses a Standard MIDI File into a `MidiFileData` struct:

- **Tempo map**: sorted list of `(abs_tick, µs_per_beat)` entries built from FF 51 meta-events. Always has a tick-0 entry (default 120 BPM).
- **Event list**: all tracks merged and stable-sorted by absolute SMF tick. Channel bits are preserved in the status byte.

`MidiFilePlayer::advance(frames, sr, vm)` is called once per audio block from both the HAL callback and `engine_process`. It converts each pending event's SMF tick to a fractional sample offset using the tempo map, dispatches it via `vm.processMidiBytes()` with the correct sub-buffer `sampleOffset`, and advances the internal playhead by `frames` samples. No `sleep()` or wall-clock timing is used.

#### Tick-to-Sample Conversion

```
tick_to_sample(T):
  walk tempo_map segments [last_tick, te.abs_tick) left-to-right
  accumulate: samples += seg_ticks × (µs_per_beat / ppq / 1e6) × sample_rate
  final segment: remaining ticks at current µs_per_beat
```

The formula is exact — no rounding or approximation beyond `double` precision.

#### Two-Phase SMF Rollout

| Phase | MIDI Channel Handling | Routing |
|---|---|---|
| 22A (current) | Ignored — all events go to one VoiceManager | All voices share the single loaded patch |
| 22B (planned) | Channel maps to a VoiceGroup | Per-channel patch assignment; Program Change triggers live patch swap |

Phase 22B depends on Phase 16 per-group patch support and is tracked separately.

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
- **Voice Groups**: Voices are partitioned into named groups, each with an independent signal chain topology defined via `engine_add_module` / `engine_connect_ports` / `engine_bake` or `engine_load_patch`. Enables keyboard splits and layering via the C API. `VoiceFactory` is retired.
- **Voice stealing**: LRU — prefer releasing voice with oldest note-on timestamp first; if no releasing voice, steal the active voice with the oldest note-on timestamp. Applied within a voice group.
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

- **is_active() Check**: A voice is considered "active" as long as its `active_` flag is set or any `AdsrEnvelopeProcessor` in its signal chain reports a non-idle state. The `VoiceManager` relies on this state to decide when a voice can be reassigned.
- **Lifecycle States**:
    - **Gate ON**: Triggers envelopes and resets oscillators (if sync is enabled).
    - **Gate OFF**: Transitions envelopes to the Release stage.
    - **Kill/Steal**: Forces an immediate (but smoothed) fade-out to reassign the voice hardware to a new note.

---

## Audio-Rate Modulation Matrix

- **Modulation as Audio**: All modulation signals (LFOs, Envelopes) are pulled at the same 48000Hz rate as the audio oscillators to ensure "zipper-free" parameter changes and FM/Filter-FM capabilities.
- **Span-Based Modulation**: To achieve true "zipper-free" performance, modulation nodes should provide a full span of modulation data per block. Processors use these spans to smoothly ramp internal parameters (e.g., Cutoff, Pitch) rather than updating once per block.
- **Target Update**: Modulation targets (Cutoff, Pitch) are recalculated per-block or per-sample using the exponential formula $f_{final} = f_{base} \cdot 2^{mod}$.

## ~~Modular Modulation Matrix~~ (RETIRED — Phase 16)

> **This section is historical. `ModulationMatrix` and `Voice::lfo_` were removed in Phase 16.** All modulation routes are now first-class named port connections in the voice graph, declared via `engine_connect_ports` or in the patch JSON `connections` array. There are no integer-ID modulation sources or targets. The description below is retained for context only.

The `ModulationMatrix` was a RT-safe central hub within each `Voice` that managed connections between sources and targets. Removed in favour of the direct port-connection graph (Phase 15/16).

### Former Core Musical Mappings (now expressed as port connections)
- **Chiff**: `ADSR_ENVELOPE.envelope_out` → VCF `cutoff_cv` with positive scaling.
- **90/10 Articulation**: ADSR gate-to-amplitude timing as a patch parameter.
- **Exponential Formula**: Both Pitch and Filter Cutoff follow $f_{final} = f_{base} \cdot 2^{mod}$ where $mod$ is the sum of CV inputs in octaves. This formula is unchanged — it is now applied inside each processor's `do_pull` rather than by a central matrix.

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

> **Note**: String labels in this table are the **bridge-facing** labels used with `set_param(handle, "label", value)` and stored in `AudioBridge::param_name_to_id`. They are intentionally different from the module-native parameter names declared by each `Processor` via `declare_parameter()` (e.g. the filter module declares `"cutoff"` and `"resonance"`, but the bridge labels are `"vcf_cutoff"` and `"vcf_res"`). The string label table is the transitional compatibility layer; Phase 16 will unify these via named port connections.
>
> Drawbar parameters (`drawbar_16`, `drawbar_513`, etc.) and `wavetable_type` are routed through the `set_parameter_by_name` fallback path (not this integer-ID table) and are therefore not listed here. See BRIDGE_GUIDE.md §3 for the full string label set.

| Global ID | String Label | Target Tag | Description | Range | Status |
|-----------|--------------|------------|-------------|-------|--------|
| 0 | `osc_frequency` | `VCO` | Override oscillator base frequency | Hz | Special case in bridge |
| 1 | `vcf_cutoff` | `VCF` | Filter cutoff frequency | Log (20Hz–20kHz) | Active |
| 2 | `vcf_res` | `VCF` | Filter resonance | 0.0–1.0 | Active |
| 3 | `vcf_env_amount` | `VCF` | Envelope to Filter modulation depth | Bipolar | **Stub** — connect ENV→filter cutoff_cv via chain port when filter moves into chain |
| 4 | `amp_attack` | `ENV` | Envelope Attack | Time (s) | Active |
| 5 | `amp_decay` | `ENV` | Envelope Decay | Time (s) | Active |
| 6 | `amp_sustain` | `ENV` | Envelope Sustain | 0.0–1.0 | Active |
| 7 | `amp_release` | `ENV` | Envelope Release | Time (s) | Active |
| 8 | *(internal)* | `Voice` | Base amplitude scale (beep/test use only) | 0.0–1.0 | Internal |
| 9 | *(reserved)* | — | Unused — gap in legacy numbering | — | Reserved |
| 10 | `osc_pw` | `VCO` | Pulse Width (legacy alias for `pulse_width`) | 0.0–0.5 | Active |
| 11 | `sub_gain` | `VCO` | Sub-oscillator level | 0.0–1.0 | Active |
| 12 | `saw_gain` | `VCO` | Sawtooth level | 0.0–1.0 | Active |
| 13 | `pulse_gain` | `VCO` | Pulse level | 0.0–1.0 | Active |
| 14 | `pulse_width` | `VCO` | Pulse Width (native) | 0.0–0.5 | Active |
| 15 | `sine_gain` | `VCO` | Sine oscillator level | 0.0–1.0 | Active |
| 16 | `triangle_gain` | `VCO` | Triangle oscillator level | 0.0–1.0 | Active |
| 17 | `wavetable_gain` | `VCO` | Wavetable oscillator level | 0.0–1.0 | Active |
| 18 | `noise_gain` | `VCO` | White noise level | 0.0–1.0 | Active |
| 19 | `wavetable_type` | `VCO` | Wavetable waveform type | 0–N (enum) | Active |

### Future Architectural Roadmap

1. **Global Modulation Bus**: Shift from per-voice matrices to a centralized bus where global sources (e.g., Vibrato LFO) write to synchronized slots, improving phase coherency and reducing CPU overhead via a subscription model.
2. **BaseOscillator Hierarchy**: Consolidate redundant logic (gain, fine-tuning, pitch-bend) into a polymorphic collection, allowing the `Voice` to manage generators as a dynamic list rather than hardcoded pointers.

### Implementation Rules
1. **Test Source of Truth**: All testing standards, including the "Golden Lifecycle" and mandatory modular routing protocols, are defined in [cxx/docs/TESTING.md](cxx/docs/TESTING.md).
1. **Bridge Duty**: The `AudioBridge` (EngineHandleImpl) MUST maintain a `param_name_to_id` map matching the "String Label" column.
2. **Voice Duty**: Parameter routing is handled by `Voice::set_parameter(int id, float value)` dispatching through `find_by_tag()` to chain nodes. The Global ID must match the registry table above.
3. **RT-Safety**: All mapping lookups occur outside the audio thread or via lock-free atomic caches.

---

## MIDI HAL Architecture (Phase 25)

The MIDI hardware layer follows the **identical factory pattern** as `AudioDriver` — platform-specific code is isolated to concrete driver classes; the bridge and core engine see only the abstract base.

### Design Principles

- **No platform `#ifdef`s in `AudioBridge.cpp`**: `MidiDriver::create()` and `MidiDriver::enumerate_devices()` are static methods declared in the base header and defined only in the platform `.cpp` files. The bridge calls these factory methods without OS-specific includes.
- **MIDI and Audio HALs are independent**: `MidiDriver` has no dependency on `AudioDriver`. They share `hal::` namespace and the same structural pattern, but neither owns the other.
- **Input is callback-driven**: MIDI data arrives asynchronously on a driver thread (ALSA: polling thread; CoreMIDI: CoreMIDI callback thread). The callback dispatches to the engine via `engine_process_midi_bytes` with `sampleOffset = 0`. Sub-block sample-accurate MIDI scheduling (computing the exact offset within the current audio block using hardware timestamps) is a Phase 25B enhancement.
- **Output is synchronous**: `send_bytes()` calls the OS write API immediately. RT-safety is caller's responsibility — do not call `send_bytes()` from the audio thread.
- **RT-safe event queue** (optional, Phase 25B): Incoming MIDI bytes can be pushed to a lock-free SPSC ring buffer, then drained by the audio callback at block start with accurate `sampleOffset` values derived from hardware timestamps.

### `HostMidiDeviceInfo` (new, analogous to `HostDeviceInfo`)

```cpp
// hal/HostMidiDeviceInfo.hpp
namespace hal {
struct HostMidiDeviceInfo {
    int         index;         // 0-based enumeration index
    std::string name;          // Human-readable name ("Arturia KeyStep", "IAC Driver Bus 1")
    bool        is_input;      // Supports MIDI input (receiving from device)
    bool        is_output;     // Supports MIDI output (sending to device)
};
} // namespace hal
```

All fields are standard C++ types — no OS handles, no platform-specific types.

### `MidiDriver` Abstract Base

```cpp
// hal/MidiDriver.hpp
namespace hal {

class MidiDriver {
public:
    // Callback receives raw MIDI bytes and a nanosecond hardware timestamp.
    // Called from the driver's background thread — must be RT-safe.
    using MidiInputCallback =
        std::function<void(const uint8_t* data, size_t size, uint64_t timestamp_ns)>;

    virtual ~MidiDriver() = default;

    virtual bool open_input(int device_index)  = 0;
    virtual bool open_output(int device_index) = 0;
    virtual void close()                        = 0;
    virtual void set_input_callback(MidiInputCallback cb) = 0;
    virtual bool send_bytes(const uint8_t* data, size_t size) = 0;
    virtual std::string device_name() const    = 0;

    // Defined in platform .cpp — no platform headers required by callers
    static std::vector<HostMidiDeviceInfo> enumerate_devices();
    static std::unique_ptr<MidiDriver>     create();
};

} // namespace hal
```

### Platform Implementations

| Platform | Driver class | Key OS APIs |
|---|---|---|
| Linux | `AlsaMidiDriver` | `snd_rawmidi_open`, `snd_rawmidi_read`, `snd_rawmidi_write`, polling thread |
| macOS | `CoreMidiDriver` | `MIDIClientCreate`, `MIDIInputPortCreate` (callback), `MIDIOutputPortCreate`, `MIDISend` |

**Linux (ALSA rawmidi)**: Opens `/dev/midi*` or ALSA rawmidi handles. Spawns a `std::thread` that calls `snd_rawmidi_read()` in a poll loop. On receipt, invokes `MidiInputCallback` with the raw bytes and `clock_gettime(CLOCK_MONOTONIC)` timestamp. All ALSA headers (`alsa/asoundlib.h`) are confined to `AlsaMidiDriver.cpp` — the `.hpp` forward-declares the ALSA handle type.

**macOS (CoreMIDI)**: Creates a `MIDIClientRef` and `MIDIPortRef` in `open_input()`. CoreMIDI dispatches received `MIDIPacketList` data to a callback on the CoreMIDI run loop thread; the driver converts packets to raw bytes and invokes `MidiInputCallback`. CoreMIDI headers (`CoreMIDI/CoreMIDI.h`) are confined to `CoreMidiDriver.cpp`.

### C API (new `midi_*` family in `CInterface.h`)

```c
// Device enumeration (no handle needed)
int  host_midi_device_count();
int  host_midi_get_device_info(int index, /* HostMidiDeviceInfo fields */ ...);

// Lifecycle
MidiHandle* midi_open_input(int device_index);
MidiHandle* midi_open_output(int device_index);
void        midi_close(MidiHandle* handle);

// Connect a MIDI input to an engine — automatically dispatches received bytes
// via engine_process_midi_bytes(engine, data, size, 0)
void        midi_connect_to_engine(MidiHandle* midi_handle, EngineHandle* engine_handle);

// Output
int         midi_send_bytes(MidiHandle* handle, const uint8_t* data, int size);
int         midi_send_note_on(MidiHandle* handle, int channel, int note, int velocity);
int         midi_send_note_off(MidiHandle* handle, int channel, int note);
int         midi_send_cc(MidiHandle* handle, int channel, int cc, int value);
int         midi_send_program_change(MidiHandle* handle, int channel, int program);
```

`MidiHandle` is an opaque pointer (same pattern as `EngineHandle`). The connection established by `midi_connect_to_engine` is live for the lifetime of the `MidiHandle` — calling `midi_close` disconnects and destroys it.

### CMake Changes

```cmake
if(APPLE)
    list(APPEND ENGINE_SOURCES src/hal/coreaudio/CoreMidiDriver.cpp)
    find_library(COREMIDI_FRAMEWORK CoreMIDI REQUIRED)
    list(APPEND PLATFORM_LIBS ${COREMIDI_FRAMEWORK})
elseif(UNIX AND NOT APPLE)
    list(APPEND ENGINE_SOURCES src/hal/alsa/AlsaMidiDriver.cpp)
    # ALSA already linked via pkg_check_modules(ALSA)
endif()
```

### Testing

- **Unit**: Mock `MidiDriver` that replays a pre-recorded byte sequence into `engine_process_midi_bytes`. Verify that note-on/off events reach the `VoiceManager`.
- **Integration**: `test_midi_roundtrip.cpp` — opens a loopback MIDI port (Linux: ALSA seq loopback; macOS: IAC bus), sends a note-on from the output port, verifies the input callback fires with the correct bytes.
- **Functional**: `Functional_MidiLive` — interactive test (not part of `ctest`) that opens a real USB MIDI device by name and plays a scale by sending note events to the engine.

---

## Static Module Configuration (Phase 26)

The goal is to produce a stripped binary suitable for resource-constrained hosts (Raspberry Pi, embedded Linux) by compiling only the processor modules needed for the target use case.

### Problem with the Current Design

`ProcessorRegistrations.cpp` calls `register_builtin_processors()` which unconditionally registers all ~30 modules. There is no way to exclude individual modules at compile time without editing this file. On a Pi Zero (512 MB RAM, single-core ARMv6), loading a full desktop build with FDN reverb, 4 different filter types, and all CV utilities wastes code space and startup time.

### Solution: Self-Registering Modules via Static Initializers

Each processor module gets its own small `.cpp` file whose sole responsibility is registering that module via a C++ static initializer. CMake decides which `.cpp` files to compile. If a `.cpp` file is not compiled in, its static initializer never runs, and the module is never registered — no `#ifdef`, no runtime cost.

#### Self-Registration Pattern

```cpp
// src/dsp/fx/DistortionProcessor_reg.cpp
#include "../../core/ModuleRegistry.hpp"
#include "DistortionProcessor.hpp"
namespace audio {
static const bool kReg = (ModuleRegistry::instance().register_module(
    "DISTORTION",
    "Guitar-style distortion — drive + character blend",
    [](int sr) { return std::make_unique<DistortionProcessor>(sr); }
), true);
} // namespace audio
```

`kReg` is a translation-unit-local variable. The linker includes the TU only if the `.cpp` is listed in `ENGINE_SOURCES`. Modules not listed simply do not exist at link time.

#### Central Registration File Fate

`ProcessorRegistrations.cpp` is **retained as the "full desktop" build path** — it registers everything and is included by default (`AUDIO_STATIC_CONFIG=OFF`). When `AUDIO_STATIC_CONFIG=ON`, `ProcessorRegistrations.cpp` is excluded from `ENGINE_SOURCES` and the per-module `_reg.cpp` files take over, controlled individually by CMake options.

This preserves the existing build exactly as-is while enabling the new embedded path without a flag.

### CMake Module Groups

Modules are organized into groups. Each group is controlled by a single CMake `option()`. Individual module granularity is available but not required for most targets.

```cmake
option(AUDIO_STATIC_CONFIG "Use per-module self-registration instead of central ProcessorRegistrations.cpp" OFF)

if(AUDIO_STATIC_CONFIG)
    # Core — always compiled, no option to disable
    list(APPEND ENGINE_SOURCES
        src/dsp/envelope/AdsrEnvelopeProcessor_reg.cpp
        src/dsp/envelope/ADEnvelopeProcessor_reg.cpp
        src/dsp/VcaProcessor_reg.cpp
        src/dsp/routing/CompositeGenerator_reg.cpp
        src/dsp/oscillator/LfoProcessor_reg.cpp
        src/dsp/oscillator/WhiteNoiseProcessor_reg.cpp
    )

    # Filters (one or more ladder types)
    option(AUDIO_MODULE_FILTERS_ALL  "All four filter types"        ON)
    option(AUDIO_MODULE_FILTER_MOOG  "Moog ladder filter"           ON)
    option(AUDIO_MODULE_FILTER_DIODE "TB-303 diode ladder"          ON)
    option(AUDIO_MODULE_FILTER_SH    "SH-101 CEM filter"            ON)
    option(AUDIO_MODULE_FILTER_MS20  "Korg MS-20 SVF"               ON)
    option(AUDIO_MODULE_FILTER_HPF   "High-pass biquad"             ON)
    option(AUDIO_MODULE_FILTER_BPF   "Band-pass biquad"             ON)

    # FX
    option(AUDIO_MODULE_REVERB       "FDN and Freeverb reverb"      ON)
    option(AUDIO_MODULE_CHORUS       "Juno BBD chorus"              ON)
    option(AUDIO_MODULE_DISTORTION   "Guitar distortion"            ON)
    option(AUDIO_MODULE_PHASER       "All-pass phaser"              ON)
    option(AUDIO_MODULE_ECHO_DELAY   "Echo/BBD delay"               ON)

    # Dynamics
    option(AUDIO_MODULE_DYNAMICS     "Noise gate + envelope follower" ON)

    # CV Utilities
    option(AUDIO_MODULE_CV_UTILS     "CV mixer, splitter, maths, S&H, gate delay, inverter" ON)

    # Audio Routing
    option(AUDIO_MODULE_AUDIO_ROUTING "Audio splitter, mixer, ring mod" ON)

    # Organ
    option(AUDIO_MODULE_DRAWBAR_ORGAN "Hammond drawbar organ"       ON)

    # Conditionally append _reg.cpp files based on options ...
endif()
```

### CMakePresets for Named Targets

`CMakePresets.json` (at project root alongside `CMakeLists.txt`) defines named configurations:

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "desktop_full",
      "displayName": "Desktop — full module set",
      "cacheVariables": {
        "AUDIO_STATIC_CONFIG": "OFF"
      }
    },
    {
      "name": "pi_minimal",
      "displayName": "Raspberry Pi — minimal (VCO, one filter, ADSR, VCA, LFO)",
      "cacheVariables": {
        "AUDIO_STATIC_CONFIG":       "ON",
        "AUDIO_MODULE_FILTER_MOOG":  "ON",
        "AUDIO_MODULE_FILTER_DIODE": "OFF",
        "AUDIO_MODULE_FILTER_SH":    "OFF",
        "AUDIO_MODULE_FILTER_MS20":  "OFF",
        "AUDIO_MODULE_FILTER_HPF":   "ON",
        "AUDIO_MODULE_FILTER_BPF":   "OFF",
        "AUDIO_MODULE_REVERB":       "OFF",
        "AUDIO_MODULE_CHORUS":       "OFF",
        "AUDIO_MODULE_DISTORTION":   "OFF",
        "AUDIO_MODULE_PHASER":       "OFF",
        "AUDIO_MODULE_ECHO_DELAY":   "OFF",
        "AUDIO_MODULE_DYNAMICS":     "OFF",
        "AUDIO_MODULE_CV_UTILS":     "OFF",
        "AUDIO_MODULE_AUDIO_ROUTING":"OFF",
        "AUDIO_MODULE_DRAWBAR_ORGAN":"OFF"
      }
    },
    {
      "name": "pi_synth",
      "displayName": "Raspberry Pi — synth (all filters, distortion, delay, CV utils)",
      "cacheVariables": {
        "AUDIO_STATIC_CONFIG":       "ON",
        "AUDIO_MODULE_REVERB":       "OFF",
        "AUDIO_MODULE_CHORUS":       "OFF",
        "AUDIO_MODULE_PHASER":       "OFF",
        "AUDIO_MODULE_DRAWBAR_ORGAN":"OFF"
      }
    }
  ]
}
```

Usage:
```bash
cmake --preset pi_minimal -B build_pi
cmake --build build_pi
# Binary contains only 8 modules vs. 30+ in desktop_full
```

### Python Configuration Tool (`tools/configure_modules.py`)

The Python tool addresses two use cases:
1. **Interactive configuration**: prompts the user to select modules and writes a `CMakeUserPresets.json`
2. **Preset validation**: checks a preset name against the known module list and reports which modules are included/excluded

```
tools/configure_modules.py --interactive
  → Prompts: "Include reverb? [Y/n]", "Include distortion? [Y/n]", ...
  → Writes: cmake/user_preset.cmake or CMakeUserPresets.json

tools/configure_modules.py --preset pi_minimal --report
  → Outputs a table: module name | included | estimated code size contribution

tools/configure_modules.py --list-presets
  → Lists all defined presets from CMakePresets.json
```

The tool is a pure Python 3 script with no external dependencies — it manipulates JSON directly. It does not replace CMake; it is a convenience wrapper over `cmake --preset`.

### Error Handling for Missing Modules

When a patch JSON references a module type that was not compiled in (e.g., `"type": "REVERB_FDN"` on a `pi_minimal` build), `ModuleRegistry::create()` returns `nullptr`. The patch loader logs:

```
[WARN] Module type "REVERB_FDN" not registered — was it excluded at compile time?
       Patch load failed for group 0.
```

This is a clear, actionable error. The patch itself is not loaded, so the engine is never in a partially-configured state.

### Binary Size Estimates

| Preset | Modules | Approx. binary size reduction |
|---|---|---|
| `desktop_full` | 30+ | baseline |
| `pi_synth` | ~20 | ~25–35% smaller |
| `pi_minimal` | ~8 | ~50–70% smaller |

Estimates based on typical DSP module object sizes (2–8 KB each before LTO). Link-time optimization (`-flto`) can reduce further.

### Testing

- **Unit**: Verify that a `pi_minimal` build's `ModuleRegistry` contains exactly the expected modules and rejects attempts to instantiate excluded ones.
- **Build CI**: Add a CMake build job to CI that builds with `pi_minimal` preset and verifies it links clean.

---

## Documentation Map

> **Context Refresh Rule**: When resuming work after a context break, read ARCH_PLAN.md (this file) first, then the documents below in order. Each document governs a distinct domain — all rules in all documents are active at all times. ARCH_PLAN.md is the single source of truth for architecture decisions; the other documents are the governing contracts for their respective domains.

| Document | Domain | Key Rules |
|----------|--------|-----------|
| [docs/GIT_POLICY.md](docs/GIT_POLICY.md) | Git workflow | Branch naming (`yyyymmddhhmm[-suffix]`), commit format, PR process, post-merge cleanup, documentation sync rule |
| [docs/TESTING.md](docs/TESTING.md) | Test standards | Golden Lifecycle, tier classification, C-API-only rule for functional tests, silence debugger protocol |
| [docs/BRIDGE_GUIDE.md](docs/BRIDGE_GUIDE.md) | C-Bridge contract | All `CInterface.h` functions with usage examples; parameter label registry; removed/deprecated API table |
| [docs/MODULE_DESC.md](docs/MODULE_DESC.md) | Processor specifications | Port names, port types, parameter declarations, connection rules, known gaps for every module type |
| [docs/PATCH_SPEC.md](docs/PATCH_SPEC.md) | Patch file format | JSON v2 schema, chain/connection/parameter structure, patch library |
| BUILD.md | Build & CI | CMake targets, dependency setup, ctest invocation |

## References
- Git workflow: [docs/GIT_POLICY.md](docs/GIT_POLICY.md) (branch naming, PR process, commit standards).
- Coding rules: repo root `.clinerules` (quick-reference; points to canonical docs above).


