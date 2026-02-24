# Testing & Telemetry in the Audio Engine

This document outlines the testing strategy and the use of the RT-Safe telemetry system for the Audio Engine.

## RT-Safe Logger

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

## Writing Tests

We use **GoogleTest** for both Unit and Integration tests.

### 1. Behavioral Testing with Telemetry

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

## Developer Workflow (Pre-Check-in)

To maintain a "Green Build," every developer must follow this loop before pushing code:

1.  **Build**: `cd cxx/build && cmake .. && make -j`
2.  **Test**: `ctest --output-on-failure` or `./bin/unit_tests`
3.  **Verify RT-Safety**: Ensure no new `printf` or `std::cout` calls were added to the audio thread (use `AudioLogger` instead).
4.  **Documentation**: Update `BRIDGE_GUIDE.md` if any C-API changes were made.

### 2. Unit Tests

Unit tests should focus on isolated components (`src/core` or `src/dsp`).
Place new tests in `cxx/tests/unit/` and register them in `cxx/CMakeLists.txt`.

Key areas to test:
- **Sample-accurate timing:** `test_clock_logic.cpp`
- **Envelope behavior:** `test_envelope_stages.cpp`
- **Stress & Load:** `test_voice_stress.cpp`

### 3. Integration Tests

Integration tests verify the C-API and the interaction between multiple components.
Place these in `cxx/tests/integration/`.

## SOP for New Components

1.  **Instrument:** Add `log_event` or `log_message` calls to critical state transitions in your new component.
2.  **Verify:** Write a test that triggers these transitions and uses the `AudioLogger` to verify they happened.
3.  **Harden:** Use "Stress Tests" (flooding parameters, triggering many voices) to ensure the component is robust.
