# Testing & Telemetry in the Audio Engine

This document outlines the testing strategy, the use of the RT-Safe telemetry system, and the mandatory protocols for functional engine testing.

---

## 1. The "Golden Lifecycle" Protocol (MANDATORY)
Every functional test in `cxx/tests/functional/` **MUST** execute these 5 steps in the specified order. Skipping these is the primary cause of silent engine failures.

1.  **Environment Init**: `test::init_test_environment()`
2.  **Engine Wrapper**: `test::EngineWrapper engine(sample_rate)`
3.  **Modular Patching**: 
    * **MANDATORY**: Connect modulator to target using `engine_connect_mod`.
    * **DEPRECATED**: `engine_set_modulation` is deprecated and must not be used in new tests.
    * Example: `engine_connect_mod(engine.get(), MOD_SRC_ENVELOPE, ALL_VOICES, MOD_TGT_AMPLITUDE, 1.0f)`
4.  **ADSR Arming**: 
    * MUST set `amp_attack`, `amp_decay`, `amp_sustain`, and `amp_release` via `set_param`.
5.  **Lifecycle Start**: `engine_start(engine.get())`

---

## 2. Modular Graph Initialization
Tests must explicitly initialize the graph according to their requirements. Do not use a "one-size-fits-all" lifecycle. Choose the appropriate Tier:

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
- **Buffer Size**: Fixed at `512 samples`.
  - Latency is calculated dynamically: `(512 / sample_rate) * 1000 ms`.
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
