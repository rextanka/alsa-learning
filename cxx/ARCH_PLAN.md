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

### Musical Dimensions

Music is modelled across four dimensions. Each maps to a dedicated module family, and all future
phases should locate new features within this framework:

| Dimension   | What it governs                                             | Module family |
|-------------|-------------------------------------------------------------|---------------|
| Pitch       | Frequency and harmonic content of the fundamental          | `COMPOSITE_GENERATOR`, `DRAWBAR_ORGAN`, `WHITE_NOISE` |
| Timbre      | Spectral shape — which overtones reach the listener        | `MOOG_FILTER`, `DIODE_FILTER`, `SH_FILTER`, `MS20_FILTER`, `HIGH_PASS_FILTER`, `BAND_PASS_FILTER` |
| Dynamics    | Amplitude level and how it evolves within a note           | `VCA`, `ADSR_ENVELOPE`, `AD_ENVELOPE` |
| Temporality | Time structure — tempo, meter, beat-relative timing, modulation rate | `LFO`, `ECHO_DELAY`, `PHASER`, `MusicalClock` (engine transport) |

**Envelopes span dynamics and temporality.** Their level parameters (peak, sustain) are dynamic;
their timing parameters (attack, decay, release) are temporal. This is not an anomaly — it reflects
that a note's loudness *evolves in time*, and the two dimensions are coupled at the note boundary.

**Temporality has two layers:**
- *Local temporal evolution* — how a note changes within its lifetime (envelope shapes, LFO cycles,
  echo trails). Self-contained; requires no external clock.
- *Global temporal structure* — where notes fall in musical time (tempo, meter, beat position).
  Governed by `MusicalClock`. Beat-division sync wires local processors to the global transport
  so the same patch sounds different at 80 BPM versus 140 BPM.

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
│  Oscillators │ Envelope │ Filter │ FX  (30 modules, all compiled)  │
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
│   │   ├── ProcessorRegistrations.cpp  # Central registration — called from engine_create(); linker bait for static lib
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
│   │   ├── dynamics/              # NoiseGate, EnvelopeFollower
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
│   ├── configure_modules.py       # Module configuration tool for embedded targets (Phase 26)
│   └── gen_test_midi.py           # Generates midi/*.mid patch test fixtures (one per patch)
├── midi/                          # SMF MIDI fixtures for patch_test (one .mid per patch JSON)
│   └── TEST_PATCHES_MIDI.md       # Patch ↔ MIDI correlation table + new test suggestions
└── tests/
    ├── functional/                # Scenario-based engine tests
    │   └── patch_test.cpp         # Generic patch+MIDI driver: --smoke (ctest) + audible HAL mode
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

## External Dependency Policy

**Criteria for accepting an external dependency** — all four must hold:

1. **Peripheral to library purpose**: The library's core value is DSP/synthesis. Dependencies that handle I/O formats, serialisation, or testing infrastructure are peripheral and acceptable. A linear algebra library or audio effects DSP library would be in-scope and therefore *not* acceptable.
2. **High hand-roll cost**: Rolling our own would require substantial ongoing maintenance (e.g., codec correctness, cross-platform file I/O edge cases) with no learning benefit.
3. **Well-established**: Has a stable public API, active maintenance, and widespread industry use. Not an experimental or single-author project.
4. **Cross-platform**: Builds cleanly on macOS, Linux, and Windows without platform-specific workarounds on our side.

**Approved dependencies:**

| Dependency | Purpose | How pulled in |
|---|---|---|
| `nlohmann/json` | Patch serialisation — JSON parse/emit | CMake `FetchContent` |
| `libsndfile` | `AUDIO_FILE_READER`/`AUDIO_FILE_WRITER` — WAV/AIFF file I/O | CMake `FetchContent` (Phase 27C) |
| `libsamplerate` | Sample-rate conversion in `AUDIO_FILE_READER` | CMake `FetchContent` (Phase 27C) |
| `GoogleTest` | Unit/integration test framework | CMake `FetchContent` |

**Windows note**: All approved dependencies are available on Windows via vcpkg or their own CMake builds. `FetchContent` pulls source and builds from scratch — no vcpkg required for CI. The only Windows-specific gap is the WASAPI audio driver, which is explicitly deferred (see Cross-Platform Strategy).

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
| 26    | **hpp/cpp Companion Split, Presets & Module Tooling**: Co-located `.cpp` files for all 30 processors (FX, routing, oscillator, envelope, filter, dynamics groups). Each `.cpp` holds the constructor body, `do_pull`, helpers, and a `kRegistered` static initializer that calls the 4-arg `register_module(type, brief, usage_notes, factory)` overload. `ProcessorRegistrations.cpp` retained as the explicit registrar called from `engine_create()` — provides authoritative registration and "linker bait" for the static library. Double-registration is safe (idempotent). Extended `register_module` 4-arg overload in `ModuleRegistry.hpp` stores `usage_notes` in `ModuleDescriptor` — prerequisite for Phase 27A introspection. `VCA.response_curve` (exponential blend) implemented; `VCA.initial_gain_cv` port wired in graph executor. `CMakePresets.json` with four named configurations (`desktop_full`, `desktop_release`, `pi_synth`, `pi_minimal`). `tools/configure_modules.py` — patch validation and module-set documentation tool (subcommands: `list`, `preset`, `validate`, `interactive`). Patch library expanded: `tom_tom.json` (FM via `fm_in`), `gong_noise_layer.json`, `thunder.json`, `group_strings.json` (using `AUDIO_MIXER`), `juno_strings.json`, `delay_lead.json`, `strings_chorus_reverb.json`, `gong_full.json`. 51/51 tests pass. | Complete |
| 27A   | **Module Introspection API**: `module_get_descriptor_json(type_name, buf, max_len)` and `module_registry_get_all_json(buf, max_len)` C API; returns JSON descriptor populated from Phase 26 extended declarations (`usage_notes`, parameter/port descriptions). Sorted alphabetically; no `EngineHandle` required — registry is read-only after static init. Works natively with Swift `JSONDecoder` and React/Tauri `JSON.parse`. `test_module_registry.cpp` (unit, 20 tests) + `test_module_introspection.cpp` (integration, 10 tests) — all structural invariants, no hardcoded module names. 51/51 tests pass. | Complete |
| 27B   | **Patch Serialization**: `engine_get_patch_json(engine, group_index, buf, max_len)`, `engine_load_patch_json(engine, json, len)`, `engine_save_patch(engine, path)` — full round-trip patch serialization. Serialization state held in `EngineHandleImpl` (bridge layer): `baked_modules`/`baked_connections` snapshot on `engine_bake()`; `tag_param_cache` accumulates every `set_tag_parameter()` call; `post_chain_type_names`/`post_chain_param_cache` mirror the post-chain. Patch format v3 extends v2 with top-level `post_chain` array; v2 files load unmodified (post_chain defaults to empty). Both `engine_load_patch` (file) and `engine_load_patch_json` (string) share a single `do_load_patch_json()` helper and accept v2 and v3. Patch fixes: `juno_strings.json` JUNO_CHORUS, `acid_reverb.json` REVERB_FDN, `delay_lead.json` ECHO_DELAY — all moved from per-voice chain to `post_chain`; `cymbal.json` ECHO_DELAY (timbral BBD shimmer, pre-VCA) left in voice chain. CMakeLists: `sync_patch_fixtures ALL` custom target added (mirrors `sync_midi_fixtures`) so patches re-sync on every build. 6 new `PatchSerializationTest` integration tests (RoundTrip, ModSources, PostChainRoundTrip, SavePatch, BufferTooSmall, JunoStringsFix). | Complete |
| 27D   | **Transport Clock & Tempo-Sync Effects**: (a) `engine_set_tempo(bpm)`, `engine_set_time_signature(num, denom)`, `engine_get_tempo()` C API; `MusicalClock` made authoritative — SMF player tempo map overrides host set, manual `engine_set_tempo` overrides when no SMF is loaded. (b) `VoiceContext` gains `bpm` (float) and `beats_per_bar` (int) fields — pre-computed before each block, passed to `do_pull`, no lock required in audio thread. (c) `ECHO_DELAY` gains `sync` (bool) + `division` (enum: `whole`, `half`, `quarter`, `eighth`, `sixteenth`, `thirtysecond`, `sixtyfourth`, `dotted_quarter`, `dotted_eighth`, `triplet_quarter`, `triplet_eighth`) parameters; when `sync=true` delay time = `(60 / bpm) × division_multiplier`; `time` parameter ignored. (d) `LFO` and `PHASER` gain the same `sync`/`division` parameters for rate locking. (e) Fully backward-compatible: `sync=false` (default) uses `time`/`rate` in seconds/Hz as today. Prerequisite for sequencer phase: tempo grid, MIDI clock output (0xF8), and pattern length in bars all depend on a canonical transport object. | Complete |
| 27E   | **MIDI-to-CV Routing + Roland Module Parity**: Arch-audit (2026-03-21) exposed that the implicit lifecycle callbacks (`on_note_on`/`on_note_off`) block gate-as-CV patterns needed by Roland System 100M patches (Fig 3-4 banjo, M-132 bias trick). Phase 27E makes keyboard state a first-class routable signal. (a) **`MIDI_CV` source module** — `pitch_cv` (1 V/oct), `gate_cv` (0/5 V, high while key held), `velocity_cv` (0–1), `aftertouch_cv` (0–1); `engine_note_on/off` drive `MIDI_CV` internal state; `SOURCE` role. (b) **`COMPOSITE_GENERATOR.pitch_base_cv`** — second pitch input port (absolute V/oct, typically from `MIDI_CV`); sums with existing `pitch_cv` (modulation offset); mirrors M-110 KBD CV + MOD CV summing junctions. (c) **`CV_MIXER.inv_out`** — always −1 × `cv_out`; M-132 INV OUT precedent; counter-phase routing without a downstream `INVERTER`. (d) **M-132 gate-bias pattern** — `MIDI_CV.gate_cv` → `CV_MIXER.cv_in` + `offset` bias replicates the −10 V bias trick used on Roland Fig 3-4; LFO re-triggers ADSR only during key-held intervals. (e) **Port additions already implemented** (arch-audit branch): `LFO.control_out_inv`, `ECHO_DELAY.time_cv`, `GATE_DELAY` enhancements (`gate_in_b`, `gate_time`, 6 s range), `COMPOSITE_GENERATOR.sync_in`/`sync_out` (hard VCO sync — see below), `COMPOSITE_GENERATOR.footage`, `_inv` port convention in `Voice::pull_mono`, `Processor::get_secondary_output()` virtual. (f) **Patch migration pass** — 29 patches reviewed one-by-one against Roland service notes after `MIDI_CV` implementation. | In Progress |
| 27C   | **I/O Processor Family & Role Classification**: (a) `ModuleDescriptor` gains a `role` field (`"SOURCE"`, `"SINK"`, `"PROCESSOR"`) inferred from declared ports at registration; `module_get_descriptor_json` / `module_registry_get_all_json` include `"role"` in JSON output. (b) Four new registered processors: `AUDIO_OUTPUT` (HAL sink — explicit chain terminator with friendly UI name; `audio_in` port), `AUDIO_INPUT` (line input / microphone source; `audio_out` port + `device_index`, `gain` parameters; currently outputs silence — live HAL routing deferred to Phase 25; associate with hardware via `engine_open_audio_input(handle, device_index)`), `AUDIO_FILE_READER` (WAV/AIFF source; `audio_out` port + `loop`, `gain`; path set via `engine_set_tag_string_param`; mono/stereo only, no compressed formats), `AUDIO_FILE_WRITER` (real-time WAV recorder sink; `audio_in` port + `max_seconds`, `max_file_mb`; path set via `engine_set_tag_string_param`). (c) `bake()` updated to allow a SINK (audio_in only, no audio_out) as the last chain node. (d) New C API: `engine_set_tag_string_param(handle, tag, name, value)`, `engine_open_audio_input(handle, device_index)`, `engine_file_writer_flush(handle)`. (e) `libsndfile` 1.2.2 + `libsamplerate` 0.2.2 added as FetchContent dependencies. (f) `target_compile_options` guarded with `if(NOT MSVC)` for all engine targets (Windows portability). Prerequisite for Phase 25 USB MIDI HAL (establishes the full I/O model). | Complete |

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
            - **Rule**: Every `signal_chain_` must begin with a **Generator** node — one of `COMPOSITE_GENERATOR`, `WHITE_NOISE`, or `DRAWBAR_ORGAN`. For multi-VCO patches, `AUDIO_MIXER` may appear as the first `PORT_AUDIO` node when its inputs are fed by `mod_sources_`-style side-chain injection (see Phase 18 multi-input executor). `SourceMixer` is **retired** — multi-waveform blending within a single VCO uses `COMPOSITE_GENERATOR`'s internal waveform mixer; cross-VCO summing uses `AUDIO_MIXER`.
            - **Reason**: The first node is responsible for clearing the buffer (initialization); subsequent nodes are Processors that perform in-place modification.
            - **Validation**: A `bake()` call must be made after chain construction. `bake()` verifies that `signal_chain_[0]` outputs `PORT_AUDIO` before the voice becomes active.
        - **Node Tagging & Discovery**:
            - **Tagging**: Each node in the chain can be assigned a unique string tag (e.g., "Filter_HP", "SubOsc").
            - **Discovery**: The `Voice` implements a discovery method. When a user sends a parameter update (e.g., "Cutoff"), the `Voice` uses the tag or processor type to find the target node(s) in its current chain.
        - **RT-Safety Guardrail**: Structural changes to the `signal_chain_` (adding/removing nodes) must only occur when the voice is **Idle** or via a thread-safe command queue. The `std::vector` must never be re-allocated while the audio thread is calling `pull()`.
        - **Lifecycle Delegation**:
            - **Contract**: The `Voice` is considered active if any `EnvelopeProcessor` node in its chain is in a non-idle state.
            - **Implementation**: `Voice::is_active()` delegates to its internal dynamic nodes, fulfilling the Voice-Manager Contract for polyphonic stealing without external synchronization risks.
        - **Strategic Versatility**: This API allows the construction of complex dual-filter architectures (MS-20 style) or minimal single-oscillator chains (SH-101 style) without changing a single line of `Voice` class code.
        - **Multi-VCO Summing**: Multiple oscillator nodes (`COMPOSITE_GENERATOR`, `WHITE_NOISE`) feed `AUDIO_MIXER` via its `audio_in_1`…`audio_in_4` injection ports. `AUDIO_MIXER` produces a single summed `audio_out` with hard-clip limiting (±1.0). Set per-input `gain_N` to 0.5 when mixing two equal-level sources to prevent clipping.
        - **Factory-Based Configuration**: Instrument chains are defined in patch files (v2 JSON) and constructed at runtime via `engine_add_module` / `engine_connect_ports` / `engine_bake`. `VoiceFactory` is retired in Phase 15 — chain construction is exclusively driven by the C API and patch loader.
- **Exponential Parameter Scaling**: Pitch and Filter Cutoff modulation follow a logarithmic/octave-based response: $f_{final} = f_{base} \cdot 2^{mod}$, where $mod$ is the sum of modulation offsets in octaves.
- **Base + Offset Accumulation**: Processors maintain a "Base" value (anchor). Each block, the `ModulationMatrix` sums all offsets (bipolar) and applies them exponentially to the base.
- **Multi-VCO Mixing**: `COMPOSITE_GENERATOR`'s internal waveform mixer historically used `tanh` soft-saturation on the summed waveform slots (Phase 13). Cross-VCO summing is now handled by `AUDIO_MIXER`, which applies hard-clip limiting (±1.0). The `SourceMixer` class is **retired** — do not reference it in new code or documentation.
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
3. **Hard VCO Sync**: See [Hard VCO Sync Design](#hard-vco-sync-design) below.

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

## hpp/cpp Companion Split, Presets & Module Tooling (Phase 26)

Phase 26 was primarily a code-organisation and tooling phase. All 30 processor types received co-located `.cpp` implementation files. `CMakePresets.json` and `tools/configure_modules.py` were added to support multi-target development.

### hpp/cpp Companion Split

Every processor was split into a header (class declaration + trivial inline methods) and a co-located `.cpp` (constructor body, `do_pull`, helper methods). This eliminates large header-only translation units and reduces incremental build times.

### Dual-Layer Self-Registration Pattern

Each processor `.cpp` adds an anonymous-namespace static initializer that calls the 4-arg `register_module` overload with `usage_notes` — the metadata prerequisite for Phase 27A introspection.

> **VCA exception**: `VcaProcessor` retains its full implementation in the header by design. Its hot path is the `static apply()` helper called directly by the graph executor (not via virtual `do_pull()`), so the implementation is intentionally header-only. `VcaProcessor.cpp` contains only the `kRegistered` static.

```cpp
// src/dsp/fx/DistortionProcessor.cpp (tail of file)
namespace {
[[maybe_unused]] const bool kRegistered = ModuleRegistry::instance().register_module(
    "DISTORTION",
    "Guitar-style distortion with pre/post emphasis and 4x oversampling",
    "Place post-VCA to replicate a synth output into a pedal. "
    "drive=8, character=0.3 for warm asymmetric grit.",
    [](int sr) { return std::make_unique<DistortionProcessor>(sr); }
);
} // namespace
```

`ProcessorRegistrations.cpp` is **retained unchanged** as the explicit registrar called from `engine_create()`. It uses the 3-arg overload and explicitly references every processor factory — this serves as "linker bait", ensuring all processor `.o` files are included in a static library link even when no external symbol from that TU is otherwise referenced.

The `kRegistered` statics fire as a bonus when the `.o` is included; `register_module` is idempotent so double-registration is safe. The per-processor `kRegistered` primarily serves as co-located documentation of the registration and as the source of `usage_notes` for Phase 27A.

> **Static library linker note**: In a static library (`libaudio_engine.a`), the linker only includes `.o` files that provide symbols referenced by the executable. A processor whose only external symbols are polymorphic methods (called via base pointer through the registry) could be dropped. `ProcessorRegistrations.cpp`'s explicit factory lambdas prevent this — they directly name each processor type, forcing the linker to retain those TUs. This is why `ProcessorRegistrations.cpp` is retained even though `kRegistered` statics exist in each `.cpp`.

### Extended `register_module` Signature

The 4-arg overload added to `ModuleRegistry.hpp`:

```cpp
void register_module(
    std::string_view type_name,
    std::string_view brief_description,
    std::string_view usage_notes,
    FactoryFn        factory_fn);
```

`usage_notes` is stored in `ModuleDescriptor::usage_notes` alongside the existing `brief` and `ports`/`parameters` collections. The Phase 27A introspection API (`module_get_descriptor_json`) exposes all of this as JSON.

### CMakePresets.json

`CMakePresets.json` at the project root defines four named configurations. All four currently compile all 30 modules — selective module stripping is deferred to a future phase:

| Preset | `CMAKE_BUILD_TYPE` | Testing | Notes |
|---|---|---|---|
| `desktop_full` | Debug | ON | Primary development build |
| `desktop_release` | Release | OFF | Optimised desktop build |
| `pi_synth` | Release | OFF | arm64 cross-compile target |
| `pi_minimal` | MinSizeRel | OFF | Embedded target |

```bash
# Configure and build using a named preset
cmake --preset desktop_full
cmake --build --preset desktop_full
ctest --preset desktop_full

cmake --preset pi_synth -DCMAKE_TOOLCHAIN_FILE=...
cmake --build --preset pi_synth
```

### Python Configuration Tool (`tools/configure_modules.py`)

A pure Python 3 script (no external dependencies) with four subcommands:

```bash
# List all 30 modules with category and description
python tools/configure_modules.py list

# Show which modules a preset includes and validate patches against it
python tools/configure_modules.py preset desktop_full --validate-patches
python tools/configure_modules.py preset pi_minimal --validate-patches

# Validate all patches in patches/ against the full module set
python tools/configure_modules.py validate

# Interactive module selection + patch validation
python tools/configure_modules.py interactive
```

The tool documents which patches are compatible with a given module set. When a patch uses a module type that a preset excludes, the tool reports it clearly — useful groundwork for when selective compilation is implemented.

### Extended Module Descriptor Metadata

The 4-arg `register_module` overload and the per-processor `kRegistered` statics provide `usage_notes` for every module. The existing `declare_parameter` / `declare_port` calls in each processor constructor populate `ModuleDescriptor::parameters` and `ModuleDescriptor::ports`. All of this feeds Phase 27A's `module_get_descriptor_json` / `module_registry_get_all_json` C API.

### VCA Enhancements

- **`response_curve` (exponential blend)**: `VCA.response_curve` [0, 1] blends between linear (0) and exponential gain law (1). Current implementation follows the Roland M-130 spec (10dB/oct, 60dB dynamic range): `g_exp = exp((g - 1) * 6.908)`, normalised so `g=1 → 1.0` (unity). Blend: `effective_gain = lerp(g, g_exp, response_curve)`. Perceptually uniform for fades and percussive decays.
- **`initial_gain_cv` port wiring**: When the `initial_gain_cv` PORT_CONTROL input is connected, its current-block value overrides the `initial_gain` parameter as a multiplicative scale on `base_amplitude_` (`scale = initial_gain * base_amplitude_`), enabling live CV control of the VCA level (e.g. velocity sensitivity).

#### Previous VCA exponential implementation (pre M-130 update)

The original `response_curve=1` used a quadratic blend rather than a true exponential:

```cpp
// Old implementation in VcaProcessor::apply() — quadratic, NOT 10dB/oct
const float g_exp = g * g;
audio[i] *= (g + response_curve * (g_exp - g)) * scale;
```

`g²` is a shallower curve than M-130 hardware. At `g=0.5` it gives `0.25` vs `≈0.032` for true 10dB/oct. The tail rolloff is slower, leaving more low-level signal audible during decay. To restore this behaviour, replace the `exp()` line in `VcaProcessor::apply()` (`src/dsp/VcaProcessor.hpp`) with `const float g_exp = g * g;` and remove `kLogRange`.

### Patch Library Expansion

Eight new patches delivered in Phase 26:

| Patch | Key modules | Technique |
|---|---|---|
| `tom_tom.json` | `COMPOSITE_GENERATOR` `fm_in` | Audio-rate FM pitch drop |
| `gong_noise_layer.json` | `RING_MOD` + `AUDIO_MIXER` | Ring mod + noise blend |
| `thunder.json` | Two noise paths + `AUDIO_MIXER` | Parallel noise mixing |
| `group_strings.json` | Dual `COMPOSITE_GENERATOR` + `AUDIO_MIXER` | Detuned ensemble |
| `juno_strings.json` | `SH_FILTER` + `JUNO_CHORUS` | Roland-style strings |
| `delay_lead.json` | `SH_FILTER` + `ECHO_DELAY` | Lead with echo |
| `strings_chorus_reverb.json` | `JUNO_CHORUS` + `REVERB_FREEVERB` | Lush stereo strings |
| `gong_full.json` | `RING_MOD` + `AUDIO_MIXER` + noise | Full gong with noise layer |

### Future: Selective Module Stripping

The CMake option (`AUDIO_STATIC_CONFIG`) and per-module enable/disable flags that would actually strip unused processors from the binary are **not yet implemented**. All four CMake presets currently compile all 30 modules. The `tools/configure_modules.py` tool models the intended future behaviour for patch compatibility documentation. Selective compilation is a candidate for a future phase (28 or later) once the current architecture is stable.

### Testing

51/51 tests pass with the Phase 26 changes. Each new patch has a corresponding functional test (`test_tom_tom_patch.cpp`, `test_gong_patch.cpp`, `test_group_strings_patch_tests.cpp`, etc.).

---

## Module Introspection & Patch Serialization (Phase 27)

Phase 27 has four sub-deliverables. **All four sub-phases (27A, 27B, 27C, 27D) are complete**. 27D established the transport clock that the MIDI HAL (Phase 25) and sequencer phases will depend on.

---

### Phase 27A — Module Introspection API

Exposes the full `ModuleDescriptor` (type name, brief, usage notes, parameter descriptions, port descriptions) via a stable C API so host applications can build dynamic patch editors, auto-generate help text, and validate connections without hardcoded module knowledge.

#### C API

```c
// CInterface.h additions
//
// Returns the JSON descriptor for a single registered module type.
// Writes at most max_len bytes to buf (null-terminated).
// Returns: bytes written (excluding null), or -1 if type not found, or -2 if buf too small.
int module_get_descriptor_json(const char* type_name, char* buf, int max_len);

// Returns a JSON array of descriptors for every registered module.
// Writes at most max_len bytes to buf (null-terminated).
// Returns: bytes written (excluding null), or -2 if buf too small.
int module_registry_get_all_json(char* buf, int max_len);
```

#### JSON Descriptor Format

```json
{
  "type_name":   "MOOG_FILTER",
  "brief":       "Moog 4-pole ladder lowpass filter",
  "usage_notes": "Set resonance > 0.8 for self-oscillation. Connect ENV→cutoff_cv for filter sweep.",
  "parameters": [
    { "name": "cutoff",    "description": "Cutoff frequency (Hz, log scale 20–20000)", "min": 20.0,  "max": 20000.0, "default": 1000.0 },
    { "name": "resonance", "description": "Resonance / Q [0–1]; >0.9 self-oscillates",  "min": 0.0,   "max": 1.0,    "default": 0.2   }
  ],
  "ports": [
    { "name": "audio_in",  "type": "PORT_AUDIO",   "direction": "PORT_INPUT",  "description": "Mono audio input" },
    { "name": "audio_out", "type": "PORT_AUDIO",   "direction": "PORT_OUTPUT", "description": "Filtered audio output" },
    { "name": "cutoff_cv", "type": "PORT_CONTROL", "direction": "PORT_INPUT",  "description": "Cutoff modulation (1V/oct)" },
    { "name": "fm_in",     "type": "PORT_AUDIO",   "direction": "PORT_INPUT",  "description": "Audio-rate FM input" }
  ]
}
```

The `module_registry_get_all_json` response is a JSON array of the above objects. Both calls are safe to call from any thread (the registry is read-only after `bake()`).

#### Host Integration

- **Swift**: `JSONDecoder` + `Codable` struct matching the schema above.
- **React/Tauri**: `JSON.parse` directly; TypeScript interface generated from the schema.
- The API is a one-shot read at startup — modules do not change after registration.

#### Testing (Complete — 30 tests, 51/51 suite passes)

- **`tests/unit/test_module_registry.cpp`** (20 tests): For every registered module, asserts non-empty `brief` and `usage_notes`, valid port types/directions, parameter range sanity (`min ≤ default ≤ max`), and factory produces a live instance at 44100 and 48000 Hz. Also exercises `module_get_descriptor_json` and `module_registry_get_all_json` C API per-module.
- **`tests/integration/test_module_introspection.cpp`** (10 tests): `module_get_descriptor_json` error codes (-1 unknown type, -2 buf too small), sorted array guarantee, count consistency with `ModuleRegistry::instance().size()`, required JSON fields, single vs. bulk lookup consistency.

All tests are structural invariants — no module names or counts are hardcoded.

---

### Phase 27B — Patch Serialization

Enables saving the current engine patch state to JSON and reloading it later — round-trip fidelity for all connection types.

#### C API

```c
// Serialize the patch for a single voice group to JSON.
// Returns bytes written, or -2 if buf too small.
int  engine_get_patch_json(EngineHandle* engine, int group_index,
                           char* buf, int max_len);

// Load a patch from an in-memory JSON string (same format as v2 patch files).
// Returns 0 on success, non-zero on error.
int  engine_load_patch_json(EngineHandle* engine, const char* json, int json_len);

// Serialize the current patch for group 0 directly to a file path.
// Returns 0 on success.
int  engine_save_patch(EngineHandle* engine, const char* path);
```

#### Serialization Scope

A complete patch serialization must capture:

| Source | Why it must be walked |
|---|---|
| `signal_chain_` | PORT_AUDIO nodes (generators, filters, VCA) |
| `mod_sources_` | PORT_CONTROL generators (LFOs, envelopes) — **not reachable from audio output** |
| `connections_` | All named port connections (both audio-rate and control-rate wires) |
| `parameters_`  | Current `apply_parameter` state for each node |
| post-chain entries | `engine_post_chain_push` effects (JUNO_CHORUS, REVERB_FDN, etc.) |

Graph traversal starting from the audio output node is **insufficient** — `mod_sources_` entries (LFOs, envelopes wired only to PORT_CONTROL inputs) have no audio-path predecessor and would be silently omitted. The serializer must iterate both lists explicitly.

#### Output Format

The serialized JSON is a valid v2 patch file — it can be loaded with `engine_load_patch` (file path) or `engine_load_patch_json` (in-memory string) interchangeably.

#### Testing

- **Round-trip unit test**: Load `juno_pad.json`, serialize to buffer via `engine_get_patch_json`, load result via `engine_load_patch_json`, play a note, assert RMS output matches original patch.
- **mod_sources_ coverage**: Use a patch with a standalone LFO (not connected to any audio-path node). Serialize, reload, verify LFO modulation is present in the reloaded patch.
- **Post-chain round-trip**: Push `REVERB_FDN` onto the post-chain, serialize, reload, verify the post-chain entry is preserved.

---

## I/O Processor Family & Role Classification (Phase 27C) — Complete

Phase 27C completed the I/O model before Phase 25 (USB MIDI HAL) by giving every module an explicit **role** and adding the four I/O processors that host applications (patch editors, guitar rig apps, offline renderers) need.

### Role Classification

Add a `role` field to `ModuleDescriptor`, inferred automatically at `register_module` time from the declared port set:

| Role | Criteria | Examples |
|---|---|---|
| `SOURCE` | PORT_AUDIO output, no PORT_AUDIO input | `WHITE_NOISE`, `DRAWBAR_ORGAN`, `AUDIO_INPUT`, `AUDIO_FILE_READER` |
| `SINK` | PORT_AUDIO input, no PORT_AUDIO output | `AUDIO_OUTPUT`, `AUDIO_FILE_WRITER` |
| `PROCESSOR` | Both PORT_AUDIO input and output, OR no PORT_AUDIO at all (pure CV) | All filters, FX, VCA, routing nodes; also `LFO`, `ADSR_ENVELOPE`, `AD_ENVELOPE`, `CV_MIXER`, `CV_SPLITTER`, `MATHS`, `GATE_DELAY`, `SAMPLE_HOLD`, `INVERTER`. `COMPOSITE_GENERATOR` has `fm_in` (PORT_AUDIO in) + `audio_out` → PROCESSOR. |

`module_get_descriptor_json` and `module_registry_get_all_json` add `"role"` to the JSON output. No per-processor changes needed — the role is computed once per registration.

```json
{ "type_name": "COMPOSITE_GENERATOR", "role": "SOURCE", "brief": "...", ... }
{ "type_name": "AUDIO_OUTPUT",        "role": "SINK",   "brief": "...", ... }
{ "type_name": "MOOG_FILTER",         "role": "PROCESSOR", ... }
```

### New I/O Processors

#### `AUDIO_OUTPUT` (SINK)
Explicit HAL output sink. Provides a chain-terminator node with a friendly name for UI tools. The engine's implicit "last node feeds SummingBus" behaviour is preserved for backward compatibility — `AUDIO_OUTPUT` is additive, not required.
- **Ports**: `PORT_AUDIO` in `audio_in`
- `bake()` exception: SINK nodes are allowed as the last chain entry (existing "last node must output PORT_AUDIO" check skipped for SINK role).

#### `AUDIO_INPUT` (SOURCE)
Live audio from hardware line input or microphone.
- **Ports**: `PORT_AUDIO` out `audio_out`
- **Parameters**: `device_index` (int, default 0), `gain` (0.0–4.0, default 1.0)
- **Platform**: CoreAudio full-duplex (native); ALSA capture PCM (separate `snd_pcm_open(SND_PCM_STREAM_CAPTURE)`)
- **C API**: `engine_open_audio_input(handle, device_index)` — opens the capture path and binds it to the `AUDIO_INPUT` node.
- **Current status**: Currently outputs silence. Live HAL routing is deferred to Phase 25. The processor is registered and accepted by `bake()` as a SOURCE at the chain head.

#### `AUDIO_FILE_READER` (SOURCE)
WAV/AIFF file playback source for offline processing or sample replay in a chain.
- **Ports**: `PORT_AUDIO` out `audio_out`
- **Parameters**: `loop` (bool, default false), `gain` (0.0–4.0, default 1.0)
- **String parameters**: `path` — set via `engine_set_tag_string_param(handle, tag, "path", "/file.wav")` after `bake()`.
- File is loaded into memory at engine start; sample-rate conversion via libsamplerate is applied if the file sample rate differs from the engine sample rate. Underrun produces silence.
- Mono and stereo files only. No compressed formats (MP3, AAC, OGG).

#### `AUDIO_FILE_WRITER` (SINK)
Real-time WAV recorder. Captures everything arriving at `audio_in` to a file on disk.
- **Ports**: `PORT_AUDIO` in `audio_in`
- **Parameters**: `max_seconds` (0–86400, default 0 = unlimited), `max_file_mb` (0–4096, default 0 = unlimited)
- **String parameters**: `path` — set via `engine_set_tag_string_param(handle, tag, "path", "/output.wav")` after `bake()`.
- **C API**: `engine_file_writer_flush(handle)` — explicitly syncs all `AUDIO_FILE_WRITER` processors. Auto-flush occurs on `engine_destroy`.
- Last node in chain only (SINK role). `bake()` SINK exception applies.

### Mono-to-Stereo Paths

The engine is mono-until-SummingBus by design. Two explicit paths allow stereo width before the bus:

1. **Stereo FX left-channel use**: Processors with stereo internal processing (`JUNO_CHORUS`, `REVERB_FDN`, `REVERB_FREEVERB`) produce a stereo field in their `audio_out` when placed in the global post-chain. Within a per-voice chain they operate mono.
2. **`AUDIO_SPLITTER` explicit copy**: Connect `VCO.audio_out → AUDIO_SPLITTER.audio_in`, then `audio_out_1` → left path and `audio_out_2` → right path of a stereo post-chain processor. This produces two identical mono copies that a downstream spatial processor can pan independently.

### Testing

- `role` field present in every module's JSON output (extend `test_module_registry.cpp`); valid values are `SOURCE`, `SINK`, `PROCESSOR` only
- All SOURCE modules have no PORT_AUDIO input; all SINK modules have no PORT_AUDIO output; pure CV modules (LFO, ADSR, etc.) have role PROCESSOR
- `AUDIO_OUTPUT` accepted as last chain node by `bake()` without error
- `AUDIO_INPUT` chain produces non-silent output when a capture device is available
- `AUDIO_FILE_WRITER` produces a valid WAV file containing the rendered frames

---

## Transport Clock & Tempo-Sync Effects (Phase 27D — Complete)

Establishes a canonical, engine-owned tempo and time-signature state that effects processors can read at audio rate without locks, and that the SMF player, sequencer, and MIDI clock output all write to from the same source.

### Transport State

`MusicalClock` (already in `src/core/MusicalClock.hpp`) is promoted to the authoritative transport object. Two write paths, one read path:

- **Host/UI write**: `engine_set_tempo(bpm)` / `engine_set_time_signature(num, denom)` — used when no SMF file is playing.
- **SMF player write**: `MidiFilePlayer` extracts FF 51 tempo meta-events and calls `MusicalClock::set_tempo()` automatically during playback. This overrides any host-set tempo while the file is playing.
- **Processor read**: `VoiceContext` carries `float bpm` and `int beats_per_bar` pre-computed once per block from `MusicalClock`. Processors access these inside `do_pull(span, ctx)` — zero allocations, zero locks.

#### New C API

```c
// Set tempo manually (ignored while SMF is playing)
void engine_set_tempo(EngineHandle* h, float bpm);

// Set time signature (affects bar-length calculations and sequencer grid; default 4/4)
void engine_set_time_signature(EngineHandle* h, int numerator, int denominator);

// Query current live tempo (reflects SMF file tempo if playing, else manually set value)
float engine_get_tempo(EngineHandle* h);
```

### Tempo-Sync Division Vocabulary

All synced effects share the same `division` enum. Divisions are **beat-relative** (not bar-relative) so they work correctly in any time signature:

| `division` value | Multiplier (× one beat) | At 120 BPM |
|---|---|---|
| `"whole"` | 4.0 | 2000 ms |
| `"half"` | 2.0 | 1000 ms |
| `"quarter"` | 1.0 | 500 ms |
| `"dotted_quarter"` | 1.5 | 750 ms |
| `"eighth"` | 0.5 | 250 ms |
| `"dotted_eighth"` | 0.75 | 375 ms — classic "Edge delay" |
| `"triplet_quarter"` | 0.667 | 333 ms |
| `"sixteenth"` | 0.25 | 125 ms |
| `"triplet_eighth"` | 0.333 | 167 ms |
| `"thirtysecond"` | 0.125 | 62.5 ms |
| `"sixtyfourth"` | 0.0625 | 31.25 ms |

`delay_time_seconds = (60.0 / bpm) × multiplier`

### Processors Gaining Tempo-Sync Parameters

All three additions are **backward-compatible**: `sync` defaults to `false`, leaving current `time`/`rate` parameters fully in control.

#### `ECHO_DELAY`

New parameters added alongside existing `time` and `mod_rate`:

| Parameter | Type | Default | Description |
|---|---|---|---|
| `sync` | bool | `false` | When `true`, `time` is ignored; delay length is computed from `bpm` + `division` |
| `division` | enum | `"quarter"` | Beat subdivision (see table above) |

When `sync=true`: `time = (60 / ctx->bpm) × division_multiplier`, recomputed each block if tempo changes (smooth ramp via SmoothedParam to avoid click on tempo change).

#### `LFO`

New parameters added alongside existing `rate`:

| Parameter | Type | Default | Description |
|---|---|---|---|
| `sync` | bool | `false` | When `true`, `rate` is ignored; LFO period = `(60 / bpm) × division_multiplier` |
| `division` | enum | `"quarter"` | One LFO cycle per N beats |

Synced LFO at `"whole"` division = one slow sweep per bar. Useful for tempo-locked filter sweeps, tremolo, and vibrato.

#### `PHASER`

New parameters added alongside existing `rate`:

| Parameter | Type | Default | Description |
|---|---|---|---|
| `sync` | bool | `false` | When `true`, `rate` is ignored; sweep rate = `(60 / bpm) × division_multiplier` |
| `division` | enum | `"half"` | Sweep cycle length in beats |

### Relationship to Future Phases

- **Phase 25 (USB MIDI HAL)**: MIDI clock output (`0xF8` timing tick, 24 PPQ) is derived directly from `MusicalClock`. No additional transport infrastructure needed once 27D lands.
- **Sequencer phase**: Step sequencer grid uses `time_signature.numerator` as steps-per-bar. Pattern length in bars uses `MusicalClock::bar_duration_seconds()`. MIDI clock sync input (external device driving the engine clock) replaces `engine_set_tempo`.

### Testing

- `engine_set_tempo(120)` + `ECHO_DELAY` with `sync=true, division="quarter"` → delay time = 500 ms ± 1 sample
- `engine_set_tempo(90)` after playback starts → delay smoothly glides to new time (no click)
- SMF playback with FF 51 tempo change → `engine_get_tempo()` reflects new tempo during block containing the event
- LFO `sync=true, division="whole"` at 120 BPM → LFO period = 2.0 s ± 1 sample
- `sync=false` on all processors → identical behaviour to Phase 27C baseline (regression)

---

## Hard VCO Sync Design

Hard sync resets the slave oscillator's phase to zero each time the master oscillator completes a cycle (crosses zero in the positive direction). This creates the classic "sync sweep" timbral effect — sweeping the slave's frequency while it is synced to the master produces waveform folding and complex harmonic evolution (Roland §2-7, Oberheim synthesizers).

### Signal Flow

```
Master VCO (fixed frequency)  ──sync_out──▶  Slave VCO (swept frequency)
                                                   │
                                                  audio_out ──▶ VCF ──▶ VCA
```

### Design

#### New Port: `sync_in` (PORT_AUDIO, input)

The slave oscillator declares a new `sync_in` port of type `PORT_AUDIO`. This port carries a trigger signal: a positive zero-crossing in the `sync_in` buffer causes the slave to reset its phase accumulator to 0.0 at that sample.

The `sync_in` signal is **the master's raw audio output** — no separate trigger generator is needed. The slave detects the positive zero-crossing internally (previous sample < 0, current sample ≥ 0).

#### New Port: `sync_out` (PORT_AUDIO, output)

The master oscillator declares a `sync_out` port that aliases its `audio_out` buffer (no copy). This naming makes the patch graph intent explicit: wiring `master.sync_out → slave.sync_in` is semantically correct even if the buffer is shared.

Alternatively, the master's `audio_out` can be split via `AUDIO_SPLITTER` — one branch to the signal path, one to `sync_in`. Both approaches are valid patch designs.

#### Phase Reset Implementation

Inside `CompositeGenerator::pull()`, if `sync_in` is connected:
```cpp
for (size_t i = 0; i < frames; ++i) {
    if (sync_in_prev_ < 0.0f && sync_in_buf[i] >= 0.0f)
        phase_ = 0.0;            // hard reset
    sync_in_prev_ = sync_in_buf[i];
    // ... normal oscillator update
}
```

Sample-accurate reset within the block — no inter-block latency.

#### Impact on `declare_port`

`COMPOSITE_GENERATOR` gains two new port declarations:
```cpp
declare_port("sync_in",  PORT_AUDIO, PORT_INPUT,  "Hard sync trigger — connect to master oscillator audio_out");
declare_port("sync_out", PORT_AUDIO, PORT_OUTPUT, "Alias of audio_out for sync routing clarity");
```

`sync_out` is a read-only alias; writing to it has no effect. The graph executor does not need special handling — `sync_out` simply returns the same `AudioBuffer*` as `audio_out`.

#### Patch Example (`patches/sync_lead.json`)

```json
{
  "version": 2,
  "name": "Sync Lead",
  "groups": [{
    "id": 0,
    "chain": [
      { "type": "COMPOSITE_GENERATOR", "tag": "MASTER" },
      { "type": "AUDIO_SPLITTER",       "tag": "SPL"    },
      { "type": "COMPOSITE_GENERATOR", "tag": "SLAVE"  },
      { "type": "MOOG_FILTER",          "tag": "VCF"   },
      { "type": "ADSR_ENVELOPE",        "tag": "ENV"   },
      { "type": "VCA",                  "tag": "VCA"   }
    ],
    "connections": [
      { "from_tag": "MASTER", "from_port": "audio_out",  "to_tag": "SPL",   "to_port": "audio_in"   },
      { "from_tag": "SPL",    "from_port": "audio_out_0","to_tag": "SLAVE", "to_port": "sync_in"    },
      { "from_tag": "SLAVE",  "from_port": "audio_out",  "to_tag": "VCF",   "to_port": "audio_in"   },
      { "from_tag": "VCF",    "from_port": "audio_out",  "to_tag": "VCA",   "to_port": "audio_in"   },
      { "from_tag": "ENV",    "from_port": "envelope_out","to_tag": "VCA",  "to_port": "gain_cv"    }
    ],
    "parameters": {
      "MASTER": { "saw_gain": 1.0 },
      "SLAVE":  { "saw_gain": 1.0 },
      "VCF":    { "cutoff": 3000.0, "resonance": 0.3 },
      "ENV":    { "attack": 0.01, "decay": 0.2, "sustain": 0.7, "release": 0.3 }
    }
  }]
}
```

The `MASTER` oscillator is tuned to the `kybd_cv` (auto-injected at note-on). The `SLAVE` oscillator is swept by a second LFO or manual `cutoff_cv` to produce the sync sweep effect. `kybd_cv` is auto-injected to `SLAVE.pitch_cv` as well (same note number, but the sync reset still dominates the timbre).

#### Scope and Placement

Hard VCO sync is **implemented** as part of Phase 27E (arch-audit branch, 2026-03-21):
- `sync_out` (PORT_AUDIO out): emits per-block trigger buffer of 1.0f pulses at every saw-phase-wrap sample, exposed via `Processor::get_secondary_output("sync_out")`.
- `sync_in` (PORT_AUDIO in): `CompositeGenerator::do_pull` checks `sync_in_buf` for positive zero-crossings each sample and resets `phase_` to 0.0 at that sample — fully sample-accurate.
- `Voice::pull_mono` routes the sync buffer via a dedicated loop that calls `get_secondary_output("sync_out")` on the master and `inject_audio("sync_in", …)` on the slave after the master has been pulled.
- `AUDIO_SPLITTER` path (one branch to signal, one to `sync_in`) is an equally valid alternative as described in the patch example above.

The implementation matches the design spec exactly. See `src/dsp/routing/CompositeGenerator.cpp` and `src/core/Voice.cpp`.

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
| [docs/LIBRARY_USER_MANUAL.md](docs/LIBRARY_USER_MANUAL.md) | Developer user guide | Module catalog, patch cookbook, analysis tools, USB MIDI HAL (§12), static module config / embedded targets (§13), Known Limitations (§14) |
| BUILD.md | Build & CI | CMake targets, dependency setup, ctest invocation |
| [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md) | External dependencies | Approved deps, versions, licenses, what-breaks-without-it, policy for adding new deps |

## References
- Git workflow: [docs/GIT_POLICY.md](docs/GIT_POLICY.md) (branch naming, PR process, commit standards).
- Coding rules: repo root `.clinerules` (quick-reference; points to canonical docs above).


