# Testing & Telemetry in the Audio Engine

This document outlines the testing strategy, the use of the RT-Safe telemetry system, and the mandatory protocols for functional engine testing.

---

## 0. The Fundamental Testing Principle

> **Functional tests are consumer contracts. They use only `CInterface.h`.**

- **Functional tests** (`tests/functional/`) must import no C++ headers beyond `TestHelper.hpp` and `CInterface.h`. No internal types, no `Voice`, no `VoiceFactory`, no `Processor` subclasses. If a behaviour cannot be exercised through the C API it is not a supported feature — expose it through the API first.
- **Unit tests** (`tests/unit/`) may use internal C++ classes directly. They test implementation detail, not consumer contract.
- **Integration tests** (`tests/integration/`) test the bridge layer — they use the C API but may also inspect internal state via the logger.

**Audit rule**: Any functional test that imports a C++ header other than `TestHelper.hpp` is non-compliant and must be rewritten or moved to unit/integration.

**Implication for `VoiceFactory`**: `VoiceFactory` is a C++ internal. Functional tests must not reference it. Chain construction in functional tests uses `engine_add_module` / `engine_connect_ports` / `engine_bake` or `engine_load_patch`.

---

## 1. The "Golden Lifecycle" Protocol (MANDATORY)
Every functional test in `cxx/tests/functional/` **MUST** execute these steps in the specified order. Skipping these is the primary cause of silent engine failures.

1.  **Environment Init**: `test::init_test_environment()`
2.  **Sample Rate Query**: `int sr = test::get_safe_sample_rate(0)` — never hardcode.
3.  **Engine Wrapper**: `test::EngineWrapper engine(sr)`
4.  **Chain Construction**: Load a patch or build a chain explicitly:
    * **Preferred**: `engine_load_patch(engine.get(), "patches/sh_bass.json")`
    * **Explicit**: `engine_add_module` → `engine_connect_ports` → `engine_bake`
    * **Removed**: `engine_set_modulation` is removed. Do not use it.
5.  **Parameter Init**: Set any patch-specific overrides via `set_param`.
6.  **Lifecycle Start**: `engine_start(engine.get())`

---

## 2. Modular Graph Initialization
Tests must explicitly initialize the graph according to their requirements. Do not use a "one-size-fits-all" lifecycle. Choose the appropriate Tier:

### Signal Chain Standard: Mono-until-Mixer

Each voice is a strictly mono signal path internally (`COMPOSITE_GENERATOR → ADSR_ENVELOPE → VCA`). Stereo width is introduced at the engine output level, not inside a voice.

```
Voice 0  (mono) ──┐
Voice 1  (mono) ──┤  SummingBus  ──► Stereo L/R  ──► Global FX  ──► HAL / engine_process
Voice N  (mono) ──┘  (panning)
```

*   **Mono Voice**: Each voice operates on a single `std::span<float>`. No voice produces stereo output directly.
*   **SummingBus**: Aggregates all active mono voices into a stereo `AudioBuffer` by applying constant-power panning per voice (`L = cos θ, R = sin θ`).
*   **Global FX**: Chorus, delay, etc. are applied to the stereo bus output after summing.
*   **`engine_process`**: Routes through the same stereo `SummingBus` path as the HAL callback. The output buffer must be `frames × 2` floats (interleaved `[L₀, R₀, L₁, R₁, …]`).

> **Rule**: never set gain, filter, or pan parameters inside a voice chain for stereo width. Pan is set per-voice via `engine_set_note_pan(handle, note, pan)` where `pan ∈ [-1.0, +1.0]`. The default `voice_spread` is 0.5 (voices alternate ±0.5 pan on each note-on).

- **Tier 1 (Basic Path)**: `COMPOSITE_GENERATOR -> ADSR_ENVELOPE -> VCA -> Output`.
  - The minimum viable chain. All tests require this — there is no shorter path.
  - Required: `engine_add_module` (VCO, ENV, VCA) / `engine_connect_ports("ENV", "envelope_out", "VCA", "gain_cv")` / `engine_bake` / `engine_start` / `set_param` (oscillator gain + `amp_sustain`).
- **Tier 2 (Modulated Path)**: Tier 1 chain with optional LFO modulation.
  - Add LFO via `engine_add_module("LFO", "LFO")` then wire with `engine_connect_ports("LFO", "cv_out", "<target>", "<port>")`.
  - `engine_set_lfo_*` (Phase 15A) is retired — do not use it in new tests.
- **Tier 3 (Complex Path)**: Complex signal chains — multiple modulators, filters as chain nodes, global post-chain FX.
  - Examples: `COMPOSITE_GENERATOR → MOOG_FILTER/DIODE_FILTER/SH_FILTER/MS20_FILTER → ADSR_ENVELOPE → VCA`, or chains with `INVERTER`, `CV_MIXER`, `RING_MOD`, or post-chain `REVERB_FDN`/`PHASER`.
  - Required: Tier 1 requirements + additional module(s) explicitly wired via `engine_connect_ports`.

---

## 3. Mandatory "Graph-Aware" Checklist
Before a test is executed, the developer (or Cline) must confirm:
1. **Graph Definition**: Is the signal path documented in the `PRINT_TEST_HEADER`?
2. **Lifecycle State**: Has `engine_start()` been called for this specific configuration?
3. **Connectivity**: Is the audio signal path wired via `engine_connect_ports` + `engine_bake`? If optional LFO modulation (LFO→pitch, LFO→cutoff) is needed, use `engine_add_module("LFO", ...)` + `engine_connect_ports` (Phase 16 API). `engine_set_lfo_*` is retired.
4. **Gain Stage**: Is the gain stage of every module in the graph explicitly initialized to a non-zero value?

---

## 4. Silence Debugger Protocol

If a test produces no audio, follow this binary search path — do not guess:

1. **Check Lifecycle**: Is `engine_bake()` called after `engine_add_module` / `engine_connect_ports`? Is `engine_start()` called?
2. **Check ENV→VCA link**: Is `ENV.envelope_out` connected to `VCA.gain_cv`? (`engine_connect_ports`)
3. **Check Gate**: Is `amp_sustain` set to a non-zero value?
4. **Check Mixer**: Are the oscillator gains (e.g., `sine_gain`) set to > 0.0f?
5. **Check AudioTap**: If using `engine_audiotap_read`, ensure `engine_audiotap_reset` was called and the tap buffer has been filled by at least one `engine_process` call.

---

## 5. Test Registry

### Unit & Integration (GTest)

| Test File | Suite | Purpose |
|-----------|-------|---------|
| `tests/unit/test_module_registry.cpp` | `unit_tests` | Phase 27A: structural invariants on every registered ModuleDescriptor via C++ API — non-empty brief/usage_notes, valid port types/directions, parameter range sanity, factory produces valid instances; also exercises `module_get_descriptor_json` and `module_registry_get_all_json` |
| `tests/integration/test_module_introspection.cpp` | `integration_tests` | Phase 27A: C API contract — `module_get_descriptor_json` (unknown type → -1, tiny buf → -2, valid JSON per module) and `module_registry_get_all_json` (sorted array, count matches registry, each entry consistent with single lookup) |

### Functional Tests

| Test File | Tier | Purpose |
|-----------|------|---------|
| `automated_osc_integrity.cpp` | 2 | High-precision pitch audit (DCT) |
| `acid_live_tweak.cpp` | 3 | Interactive acid filter sweep: keyboard-driven DIODE_FILTER cutoff/resonance tweak over TB-303 riff (standalone, not ctest) |
| `audio_check.cpp` | 1 | Basic driver stability |
| `four_beeps_adsr.cpp` | 2 | ADSR articulation & lifecycle |
| `filter_sweep_test.cpp` | 3 | Moog/Diode resonant sweep |
| `guitar_tuner_verify.cpp` | 1 | Interactive pitch accuracy |
| `oscillator_integrity_test.cpp` | 1 | Solo/mute behaviour |
| `oscillator_baseline_test.cpp` | 1 | Oscillator output baseline |
| `oscillator_drone_test.cpp` | 1 | Sustained drone stability |
| `graph_audit_test.cpp` | 3 | Dynamic signal chain audit |
| `metronome_test.cpp` | 1 | Rhythmic click timing |
| `Phase10Tests.cpp` | 2 | BPM/clock and note-name API |
| `stereo_poly_test.cpp` | 2 | Polyphonic voice panning |
| `test_tremulant_preset.cpp` | 2 | LFO→pitch vibrato via chain-placed LFO node |
| `test_lfo_modulation.cpp` | 2 | LFO chain node: vibrato variance, cutoff modulation |
| `test_juno_chorus.cpp` | 2 | Juno chorus stereo separation |
| `Functional_BachMidi.cpp` | 3 | MIDI polyphony, DrawbarOrgan |
| `Functional_SH101_Live.cpp` | 3 | Patch load + live pluck sequence |
| `patch_sequence_test.cpp` | 3 | Four reference patches: SH-101 ostinato, TB-303 acid sweep, Juno pad melody, Drawbar Organ chorale |
| `processor_check.cpp` | 2 | Oscillator frequency fidelity via hysteresis zero-crossing and symmetry analysis |
| `TimingValidation.cpp` | 1 | Callback jitter measurement and sample-accurate clock drift verification |
| `test_midi_file_playback.cpp` | 3 | SMF playback tick accuracy, note dispatch (Phase 22A) |
| `test_host_devices.cpp` | 1 | HAL device enumeration: count, name, sample rate, block size (Phase 20) |
| `test_harpsichord_patch.cpp` | 3 | Harpsichord patch: INVERTER filter sweep |
| `test_dog_whistle_patch.cpp` | 3 | Dog whistle patch: pitch sweep via dual ADSR |
| `test_sh_bass_patch.cpp` | 3 | SH-101 bass patch smoke + envelope assertion |
| `test_tb_bass_patch.cpp` | 3 | TB-303 bass patch + diode filter character |
| `test_cymbal_patch.cpp` | 3 | Cymbal: ECHO_DELAY shimmer, echo tail persistence |
| `test_bowed_bass_patch.cpp` | 3 | Bowed bass: slow attack RMS rise |
| `test_brass_patch.cpp` | 3 | Brass/horn/trumpet/trombone/tuba: fast attack, filter character (Roland Fig 1-6) |
| `test_trumpet_patch.cpp` | 3 | Trumpet: brighter cutoff variant of brass topology |
| `test_trombone_patch.cpp` | 3 | Trombone: slower attack variant of brass topology |
| `test_tuba_patch.cpp` | 3 | Tuba: sub-octave saw, low cutoff variant of brass topology |
| `test_flute_patch.cpp` | 3 | Flute: attack ramp, LFO vibrato |
| `test_oboe_patch.cpp` | 3 | Oboe: pulse mix, nasal character |
| `test_juno_pad_patch.cpp` | 3 | Juno pad: slow attack, chorus character |
| `test_organ_drawbar_patch.cpp` | 3 | Organ drawbar: register blend, Bach chorale |
| `test_percussion_noise_patch.cpp` | 3 | Percussion noise: percussive decay |
| `test_violin_vibrato_patch.cpp` | 3 | Violin vibrato: attack swell, pitch modulation |
| `test_bell_patch.cpp` | 3 | Bell: RING_MOD sidebands, percussive decay |
| `test_reverb_patch.cpp` | 3 | Reverb FDN/Freeverb: wet energy, stereo correlation |
| `test_acid_reverb_patch.cpp` | 3 | Acid reverb: TB-303 two-envelope (AD_ENV→VCF, ADSR→VCA) + FDN reverb |
| `test_banjo_patch.cpp` | 3 | Banjo: Roland Fig 3-4 repeating trigger (LFO→ADSR.ext_gate_in), onset + 400ms RMS |
| `test_bass_drum_patch.cpp` | 3 | Bass drum: sine+triangle VCO + AD envelope, onset RMS |
| `test_bongo_drums_patch.cpp` | 3 | Bongo drums: resonant LP filter + AD envelope, onset RMS |
| `test_clarinet_patch.cpp` | 3 | Clarinet: pulse VCO + LP filter, signal after 12ms attack |
| `test_cow_bell_patch.cpp` | 3 | Cow bell: pulse VCO + dual resonant LP + AD envelope, onset RMS |
| `test_delay_lead_patch.cpp` | 3 | Delay lead: ECHO_DELAY in post-chain, onset RMS |
| `test_english_horn_patch.cpp` | 3 | English horn: 30ms attack, RMS after attack window |
| `test_glockenspiel_patch.cpp` | 3 | Glockenspiel: AD_ENVELOPE 1ms attack, onset signal |
| `test_gong_patch.cpp` | 3 | Gong: RING_MOD (VCO1×VCO2 at +7st) + AD envelope, onset RMS |
| `test_gong_full_patch.cpp` | 3 | Gong full: noise transient + ring-mod body, onset RMS |
| `test_group_strings_patch.cpp` | 3 | Group strings: AUDIO_MIXER + MOOG_FILTER, RMS after 430ms attack |
| `test_juno_strings_patch.cpp` | 3 | Juno strings: 350ms attack swell, RMS after warm-up |
| `test_pizzicato_violin_patch.cpp` | 3 | Pizzicato violin: AD_ENVELOPE 1ms attack, onset RMS |
| `test_rain_patch.cpp` | 3 | Rain: LP+HPF filtered noise + slow ADSR, RMS after 587ms attack |
| `test_snare_drum_patch.cpp` | 3 | Snare drum: WHITE_NOISE + LP+HPF cascade + AD envelope, onset RMS |
| `test_strings_postchain.cpp` | 3 | Strings + post-chain FX: JUNO_CHORUS stereo spread, REVERB_FDN tail energy |
| `test_thunder_patch.cpp` | 3 | Thunder: noise + 800ms attack, RMS after warm-up |
| `test_tom_tom_patch.cpp` | 3 | Tom tom: VCO + AD envelope at C3, onset RMS |
| `test_whistling_patch.cpp` | 3 | Whistling: 80ms attack, signal after attack window |
| `test_wind_surf_patch.cpp` | 3 | Wind/surf: WHITE_NOISE + MOOG_FILTER envelope, RMS after attack |
| `test_wood_blocks_patch.cpp` | 3 | Wood blocks: AD_ENVELOPE 1ms attack, onset RMS |

### patch_test — generic patch + MIDI driver

`patch_test` is a standalone binary (not a GTest file) that drives any patch with any SMF MIDI
file.  It serves two purposes:

**Smoke mode** (`--smoke` — part of `ctest`):
Renders offline, no audio hardware required.  Asserts peak output > `1e-4`.
One `smoke_<name>` ctest entry per patch (36 total).  Added by `add_patch_smoke()` in CMakeLists.

**Audible mode** (default — not part of `ctest`):
Opens the HAL, plays back via `MidiFilePlayer`, waits for completion + 1s reverb tail.
Always prints the signal chain topology (nodes + connections) before playback.

```
# Audible (real-time HAL)
./bin/patch_test --patch patches/tom_tom.json --midi midi/tom_tom.mid

# Smoke (offline, ctest-compatible)
./bin/patch_test --smoke --patch patches/tom_tom.json --midi midi/tom_tom.mid

# Help
./bin/patch_test --help
```

MIDI fixtures live in `midi/` and are generated by `tools/gen_test_midi.py`.
See `midi/TEST_PATCHES_MIDI.md` for the full patch ↔ MIDI correlation table,
musical intent for each fixture, and instructions for adding new pairs.

---

## 6. RT-Safe Logger

The `AudioLogger` is a lock-free, single-producer single-consumer ring buffer designed for use in the high-priority Audio Thread.

### How to use the Logger in C++

```cpp
#include "Logger.hpp"

// In the Audio Thread:
audio::AudioLogger::instance().log_message("MyTag", "Something happened");
audio::AudioLogger::instance().log_event("VoiceCount", 4.0f);
```

### How to use the Logger via C-API

```c
#include "CInterface.h"

// In a "Sound Toy" or external component:
audio_log_message("Toy", "Filter Initialized");
audio_log_event("Cutoff", 440.0f);
```

## 7. Behavioral Testing with Telemetry

When testing components like the `VoiceManager`, checking the output buffer is often not enough to verify complex logic like voice stealing. The `AudioLogger` allows us to verify **internal state** without the "observer effect" of slow I/O.

**Key Techniques:**
-   **Intercept Mode**: Before triggering the action, drain the logger (`while(logger.pop_entry());`).
-   **Event Matching**: Use `audio_log_event` to log numeric IDs (e.g., "VoiceSteal" with the stolen note ID as the value).
-   **Sequence Verification**: Telemetry entries are ordered. You can verify that event A happened before event B.

```cpp
TEST(VoiceStressTest, VerifyOldestIsStolen) {
    auto& logger = audio::AudioLogger::instance();
    while (logger.pop_entry()); // Intercept Mode: Clear existing logs

    // 1. Trigger stealing...
    engine.note_on(90, 0.5f);
    
    // 2. Verify via telemetry
    bool correct_note_stolen = false;
    while (auto entry = logger.pop_entry()) {
        if (std::string(entry->tag) == "VoiceSteal" && entry->value == 60.0f) {
            correct_note_stolen = true;
            break;
        }
    }
    EXPECT_TRUE(correct_note_stolen);
}
```

---

## 8. Configuration Guide (10ms MMA Target)

To maintain the **10ms MMA Latency Target**, all functional tests follow this dynamic protocol:

- **Dynamic Sample Rate**: Mandatory. Never hardcode sample rates.
- **Protocol**: 
    1. Query hardware using `host_get_device_sample_rate(0)`.
    2. Use `test::get_safe_sample_rate()` fallback logic to handle "Device Busy" or "No Device" scenarios.
    3. Initialize engine via `engine_create(sample_rate)`.
- **Buffer Size**: Queried from hardware via `host_get_device_block_size()` (Phase 20 — implemented). Never hardcode block sizes in tests.
  - Latency is calculated dynamically: `(block_size / sample_rate) * 1000 ms`.
- **Platform-Agnostic HAL**: Tests must use `EngineHandle` and `CInterface.h`.

---

## 9. Test Output Standards

Every functional test in `cxx/tests/functional/` MUST include a standardized header block using the `PRINT_TEST_HEADER` macro.

- **Header Format Requirement**:
    ```
    ================================================================
    --- TEST: [Test Name] ---
    Intent:   [Clear statement of intent]
    Chain:    [Signal path description, e.g., VCO -> VCF -> VCA]
    Expected: [Expected audible or logged result]
    Hardware: [Detected Sample Rate] | ~[Calculated Latency]ms (512 frames)
    ================================================================
    ```

---

## 10. Implementation SOP for New Components

1.  **Instrument:** Add `log_event` or `log_message` calls to critical state transitions.
2.  **Verify:** Write a test that triggers these transitions and uses the `AudioLogger` to verify.
3.  **Harden:** Use "Stress Tests" (flooding parameters, triggering many voices) to ensure the component is robust.
4.  **Green Build Requirement**: All tests must pass before merging.
5.  **Documentation**: Update `BRIDGE_GUIDE.md` if any C-API changes were made.

---

## 11. Implementation SOP (Standard Operating Procedure)

1. **Verify Tier**: Identify the simplest graph (Tier 1/2/3) for the test.
2. **C API compliance check**: If the test is functional, confirm it imports only `CInterface.h` and `TestHelper.hpp`. Flag any C++ internal include as non-compliant.
3. **Chain Construction**: Use `engine_load_patch` or `engine_add_module` / `engine_connect_ports` / `engine_bake`. Do not use `VoiceFactory` or `engine_set_modulation`.
4. **Diagnostic Audit**: When adding a TEE point (AudioTap), verify the tap is a non-destructive push (`tap->write()`).
5. **Silence-Check**: If a functional test fails with "Empty Buffer," the first audit point is the `AudioBridge` callback.
6. **Implementation Requirement**: Before adding new components, verify port declarations, parameter ranges, and connection rules against MODULE_DESC.md.
7. **SOP**: Any C-API changes require an immediate update to BRIDGE_GUIDE.md and MODULE_DESC.md (if a new module type is involved).
