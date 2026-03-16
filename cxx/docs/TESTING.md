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

### Signal Chain Standard
*   **Mono Voice**: Each voice operates mono internally.
*   **Summing Bus**: All voices are aggregated into a stereo `SummingBus` that handles constant-power panning and master gain.
*   **Effects Chain**: The Bus output is processed through global FX (Chorus, etc.) before reaching the HAL.

- **Tier 1 (Direct Path)**: `Oscillator -> Output`.
  - Required: `engine_start`, `set_param` (Gain).
- **Tier 2 (Modulated Path)**: `Oscillator -> Modulator -> Output`.
  - Required: Tier 1 requirements + `engine_connect_mod` (e.g., ADSR -> VCA).
- **Tier 3 (Complex Path)**: `Oscillator -> VCF -> VCA -> Output`.
  - Required: Tier 2 requirements + Filter/Resonance initialization.

---

## 3. Mandatory "Graph-Aware" Checklist
Before a test is executed, the developer (or Cline) must confirm:
1. **Graph Definition**: Is the signal path documented in the `PRINT_TEST_HEADER`?
2. **Lifecycle State**: Has `engine_start()` been called for this specific configuration?
3. **Connectivity**: If modulation is used, has `engine_connect_mod` been called to link the source to the target?
4. **Gain Stage**: Is the gain stage of every module in the graph explicitly initialized to a non-zero value?

---

## 4. RT-Safe Logger

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

## 5. Behavioral Testing with Telemetry

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

## 6. Configuration Guide (10ms MMA Target)

To maintain the **10ms MMA Latency Target**, all functional tests follow this dynamic protocol:

- **Dynamic Sample Rate**: Mandatory. Never hardcode sample rates.
- **Protocol**: 
    1. Query hardware using `host_get_device_sample_rate(0)`.
    2. Use `test::get_safe_sample_rate()` fallback logic to handle "Device Busy" or "No Device" scenarios.
    3. Initialize engine via `engine_create(sample_rate)`.
- **Buffer Size**: Queried from hardware via `host_get_device_block_size()` (Phase 17). Until Phase 17 lands, 512 samples is used as a temporary default — do not hardcode it in new tests.
  - Latency is calculated dynamically: `(block_size / sample_rate) * 1000 ms`.
- **Platform-Agnostic HAL**: Tests must use `EngineHandle` and `CInterface.h`.

---

## 7. Test Output Standards

Every functional test in `cxx/tests/functional/` MUST include a standardized header block using the `PRINT_TEST_HEADER` macro.

- **Header Format Requirement**:
    ```
    ================================================================
    --- TEST: [Test Name] ---
    Intent:   [Clear statement of intent]
    Chain:    [Signal path description, e.g., VCO -> VCF -> VCA]
    Expected: [Expected audible or logged result]
    Hardware: [Detected Sample Rate] | ~[Calculated Latency]ms (512 samples)
    ================================================================
    ```

---

## 8. Implementation SOP for New Components

1.  **Instrument:** Add `log_event` or `log_message` calls to critical state transitions.
2.  **Verify:** Write a test that triggers these transitions and uses the `AudioLogger` to verify.
3.  **Harden:** Use "Stress Tests" (flooding parameters, triggering many voices) to ensure the component is robust.
4.  **Green Build Requirement**: All tests must pass before merging.
5.  **Documentation**: Update `BRIDGE_GUIDE.md` if any C-API changes were made.

---

## 9. Implementation SOP (Standard Operating Procedure)

1. **Verify Tier**: Identify the simplest graph (Tier 1/2/3) for the test.
2. **C API compliance check**: If the test is functional, confirm it imports only `CInterface.h` and `TestHelper.hpp`. Flag any C++ internal include as non-compliant.
3. **Chain Construction**: Use `engine_load_patch` or `engine_add_module` / `engine_connect_ports` / `engine_bake`. Do not use `VoiceFactory` or `engine_set_modulation`.
4. **Diagnostic Audit**: When adding a TEE point (AudioTap), verify the tap is a non-destructive push (`tap->write()`).
5. **Silence-Check**: If a functional test fails with "Empty Buffer," the first audit point is the `AudioBridge` callback.
6. **Implementation Requirement**: Before adding new components, verify compatibility against ARCH_PLAN.md.
7. **SOP**: Any C-API changes require an immediate update to BRIDGE_GUIDE.md and MODULE_DESC.md (if a new module type is involved).
