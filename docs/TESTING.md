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

When testing components like the `VoiceManager`, checking the output buffer is often not enough to verify complex logic like voice stealing.

```cpp
TEST(MyTest, VerifyStealing) {
    auto& logger = audio::AudioLogger::instance();
    // 1. Setup engine...
    // 2. Trigger events that cause stealing...
    
    // 3. Verify via telemetry
    bool steal_detected = false;
    while (auto entry = logger.pop_entry()) {
        if (std::string(entry->tag) == "VoiceSteal") {
            steal_detected = true;
            break;
        }
    }
    EXPECT_TRUE(steal_detected);
}
```

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
