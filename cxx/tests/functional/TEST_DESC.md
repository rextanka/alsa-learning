# Functional Test Guide (TEST_DESC.md)

This document serves as the "Rosetta Stone" for the engine's functional testing suite. It defines the categories of tests, the global parameter mappings, and modular recipes for regenerating or building new engine tests.

---

## 1. The Test Registry

The functional tests are located in `cxx/tests/functional/` and are categorized based on their verification target. All tests follow the **HAL-Agnostic** and **Hardware-Aware** standard.

### Hardware / HAL Sanity Tests
*Verify the platform-specific audio driver and environment (ALSA on Fedora, CoreAudio on macOS).*

| Test File | Purpose | Expected Output | Status |
|-----------|---------|-----------------|--------|
| `AlsaCheck.cpp` | Validates ALSA device enumeration and capability query. | List of ALSA devices. | Compliance OK |
| `audio_check.cpp` | Verifies basic audio callback and driver stability. | Continuous sine tone. | Compliance OK |
| `TimingValidation.cpp`| Verifies callback timing consistency and jitter. | Timing stats in console. | Compliance OK |

### DSP Functional Tests
*Verify the correctness of the signal chain, parameter mappings, and modular routing.*

| Test File | Purpose | Expected Output | Status |
|-----------|---------|-----------------|--------|
| `filter_sweep_test.cpp` | Validates filter cutoff modulation and resonance behavior. | Audible filter sweep. | Compliance OK |
| `guitar_tuner_verify.cpp` | Specialized CLI tool for pitch accuracy verification across 6 strings. | Qualitative Audit (Human + Tuner). | Qualitative |
| `graph_audit_test.cpp` | Bottom-up audit of the dynamic signal chain stages. | Step-by-step RMS verification.| Compliance OK |
| `metronome_test.cpp` | Verifies sample-accurate clock and gated beep timing. | Periodic rhythmic beeps. | Compliance OK |
| `four_beeps_adsr.cpp` | Validates VCA envelope stages across multiple notes. | 4 distinct ADSR-shaped tones. | Compliance OK |
| `stereo_poly_test.cpp` | Verifies polyphonic voice stealing and stereo panning. | Moving polyphonic cluster. | Compliance OK |
| `test_sh101_chain.cpp` | Validates a full SH-101 style signal chain. | Classic monophonic bass. | Compliance OK |
| `Functional_BachMidi.cpp`| Verifies MIDI file playback and polyphonic handling. | Multi-part organ performance. | Compliance OK |

---

## 2. Global Parameter Registry

All functional tests MUST use `set_param` with the following Global IDs and string labels for consistency across the bridge and engine.

| Global ID | String Label | Target Tag | Description |
|-----------|--------------|------------|-------------|
| 1 | `vcf_cutoff` | `VCF` | Filter cutoff frequency (Hz) |
| 2 | `vcf_res` | `VCF` | Filter resonance (0.0 - 1.0) |
| 3 | `vcf_env_amount` | `VCF` | Env -> Filter modulation depth |
| 4 | `amp_attack` | `VCA` | VCA Envelope Attack (Seconds) |
| 5 | `amp_decay` | `VCA` | VCA Envelope Decay (Seconds) |
| 6 | `amp_sustain` | `VCA` | VCA Envelope Sustain (0.0 - 1.0) |
| 7 | `amp_release` | `VCA` | VCA Envelope Release (Seconds) |
| 11 | `sub_gain` | `VCO` | Sub-oscillator level (0.0 - 1.0) |
| 12 | `saw_gain` | `VCO` | Sawtooth level (0.0 - 1.0) |
| 13 | `pulse_gain` | `VCO` | Pulse level (0.0 - 1.0) |
| 14 | `pulse_width` | `VCO` | Pulse Width (0.0 - 1.0) |

---

## 3. Minimum Viable Sound Recipes

Use these C++ snippets with `CInterface.h` and `TestHelper.hpp` to build valid test configurations.

### Recipe 1: Raw Drone (Source -> Output)
*Minimalist configuration to verify signal flow.*
```cpp
int sample_rate = test::get_safe_sample_rate(0);
PRINT_TEST_HEADER("Raw Drone", "Verify signal flow.", "VCO -> Output", "Audible tone", sample_rate);

test::EngineWrapper engine(sample_rate);
set_param(engine.get(), "pulse_gain", 1.0f); 
set_param(engine.get(), "vcf_cutoff", 20000.0f);
set_param(engine.get(), "amp_sustain", 1.0f);
engine_note_on(engine.get(), 60, 1.0f);
```

### Recipe 2: Articulated Mono (Source -> VCA -> Output)
*Verifies VCA envelope functionality.*
```cpp
set_param(engine, "saw_gain", 0.8f);    
set_param(engine, "amp_attack", 0.05f); 
set_param(engine, "amp_decay", 0.2f);   
set_param(engine, "amp_sustain", 0.5f); 
set_param(engine, "amp_release", 0.1f); 
engine_note_on(engine, 48, 1.0f);
// Wait...
engine_note_off(engine, 48);
```

---

## 4. Configuration Guide

To maintain the **10ms MMA Latency Target**, all functional tests follow this dynamic protocol:

- **Dynamic Sample Rate**: Mandatory. Never hardcode sample rates.
- **Protocol**: 
    1. Query hardware using `host_get_device_sample_rate(0)`.
    2. Use `test::get_safe_sample_rate()` fallback logic to handle "Device Busy" or "No Device" scenarios.
    3. Initialize engine via `engine_create(sample_rate)`.
- **Buffer Size**: Fixed at `512 samples`.
  - Latency is calculated dynamically: $(512 / \text{sample\_rate}) * 1000$ ms.
- **Platform-Agnostic HAL**: Tests must use `EngineHandle` and `CInterface.h`.

---

## 5. Test Output Standards

Every functional test in `cxx/tests/functional/` MUST include a standardized header block for human readability. This header must be generated at the start of the test execution using the `PRINT_TEST_HEADER` macro from `TestHelper.hpp`.

- **Intent Headers**: Use the `PRINT_TEST_HEADER` macro from `TestHelper.hpp`.
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
- **Required Fields**:
    - **Purpose**: A clear statement of the test's intent.
    - **Signal Chain**: A description of the modular path being tested (e.g., VCO -> VCF -> VCA).
    - **Expected Result**: What the user should hear or what the logs should verify.
    - **Hardware Status**: Automatically outputs the detected sample rate and calculated latency.
