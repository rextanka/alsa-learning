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
  - Optional LFO routes (Phase 15A): `engine_set_lfo_rate` / `engine_set_lfo_waveform` / `engine_set_lfo_depth(LFO_TARGET_*)` / `engine_set_lfo_intensity`.
- **Tier 3 (Complex Path)**: `COMPOSITE_GENERATOR -> MOOG_FILTER/DIODE_FILTER -> ADSR_ENVELOPE -> VCA -> Output`.
  - Required: Tier 1 requirements + `MOOG_FILTER` or `DIODE_FILTER` module in chain + filter/resonance params.

---

## 3. Mandatory "Graph-Aware" Checklist
Before a test is executed, the developer (or Cline) must confirm:
1. **Graph Definition**: Is the signal path documented in the `PRINT_TEST_HEADER`?
2. **Lifecycle State**: Has `engine_start()` been called for this specific configuration?
3. **Connectivity**: Is the audio signal path wired via `engine_connect_ports` + `engine_bake`? If optional LFO modulation (LFO→pitch, LFO→cutoff) is needed, have `engine_set_lfo_*` calls been made (Phase 15A API)?
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

| Test File | Tier | Purpose |
|-----------|------|---------|
| `automated_osc_integrity.cpp` | 2 | High-precision pitch audit (DCT) |
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
| `test_sh101_chain.cpp` | 3 | SH-101 bass chain + LFO PWM |
| `test_tremulant_preset.cpp` | 2 | Phase 15A LFO→pitch vibrato via `engine_set_lfo_*` |
| `test_lfo_modulation.cpp` | 2 | LFO API error codes, vibrato variance, cutoff modulation, clear-reset |
| `test_juno_chorus.cpp` | 2 | Juno chorus stereo separation |
| `Functional_BachMidi.cpp` | 3 | MIDI polyphony, DrawbarOrgan |
| `BachOrganTest.cpp` | 3 | DrawbarOrgan register blend |
| `Functional_SH101_Live.cpp` | 3 | Patch load + live pluck seq |
| `patch_sequence_test.cpp` | 3 | Four reference patches: SH-101 ostinato, TB-303 acid sweep, Juno pad melody, Drawbar Organ chorale |
| `processor_check.cpp` | 2 | Oscillator frequency fidelity via hysteresis zero-crossing and symmetry analysis |
| `TimingValidation.cpp` | 1 | Callback jitter measurement and sample-accurate clock drift verification |

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
- **Buffer Size**: Queried from hardware via `host_get_device_block_size()` (Phase 18). Until Phase 18 lands, 512 frames is used as a temporary default — do not hardcode it in new tests.
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
