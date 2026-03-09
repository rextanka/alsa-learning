# Functional Test Guide (TEST_DESC.md)

This document is the **single source of truth** for functional engine testing. It combines the mandatory **Modular Graph Protocol** for Cline with the **System Registry** (technical specs) for the entire DSP architecture. Any test file not adhering to this protocol is considered "non-compliant."

---

## 1. The "Golden Lifecycle" Protocol (MANDATORY)
Every functional test in `cxx/tests/functional/` **MUST** execute these 5 steps in the specified order. Skipping these is the primary cause of silent engine failures.

1.  **Environment Init**: `test::init_test_environment()`
2.  **Engine Wrapper**: `test::EngineWrapper engine(sample_rate)`
3.  **Modular Patching**: 
    * MUST connect modulator to target (if using modulation): `engine_connect_mod(engine.get(), MOD_SRC_ENVELOPE, MOD_TGT_AMPLITUDE, intensity)`
4.  **ADSR Arming**: 
    * MUST set `amp_attack`, `amp_decay`, `amp_sustain`, and `amp_release`.
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

## 4. Silence Debugger Protocol (If Test is Mute)
If a test produces no audio, do not guess. Follow this binary search path:

1.  **Check Lifecycle**: Is `engine_start()` called? (Verify via log).
2.  **Check Patching**: Is the Envelope connected to the VCA (`MOD_TGT_AMPLITUDE`)? Check via `engine_get_modulation_report`.
3.  **Check Gate**: Are `amp_attack` / `amp_sustain` set to non-zero values?
4.  **Check Mixer**: Are the oscillator gains (e.g., `sine_gain`) set to > 0.0f?

---

## 5. Implementation Policy for Cline
When implementing new tests, follow these rules:

1. **Identify Tier**: "What is the simplest possible graph for this test?"
2. **Model Pattern**: Use `four_beeps_adsr.cpp` as the architecture blueprint.
3. **Declare Path**: Write the signal path in the test header.
4. **Verify Connections**: Before debugging signal values, verify the modulation matrix. Explicit Patching: If the path involves modulation, verify the connection exists in the code *before* debugging DSP values.
5. **Minimalism**: If the test is Tier 1, do not attempt to configure ADSR/Filter parameters.
6. **Evidence-Based Debugging**: If a test is silent, implement diagnostic logs in `Voice::do_pull` to isolate the break in the chain. **Do not modify DSP logic until evidence is collected.**

---

## 6. The Test Registry (Historical)
*Use these as reference templates for your Tiers.*

| Test File | Tier | Purpose | Status |
|-----------|------|---------|--------|
| `automated_osc_integrity.cpp`| 2 | High-precision pitch audit using zero-padded DCT. | **CRITICAL** |
| `audio_check.cpp` | 1 | Basic driver stability. | OK |
| `four_beeps_adsr.cpp` | 2 | ADSR articulation & lifecycle. | OK |
| `filter_sweep_test.cpp`| 3 | Filter/Resonance modulation. | OK |
| `guitar_tuner_verify.cpp`| 1 | Pitch accuracy verification. | OK |
| `oscillator_integrity_test.cpp` | 1 | Solo/mute behavior. | OK |
| `graph_audit_test.cpp` | 3 | Dynamic signal chain audit. | OK |
| `metronome_test.cpp` | 1 | Rhythmic beep timing. | OK |
| `stereo_poly_test.cpp` | 2 | Polyphonic voice/panning. | OK |
| `test_sh101_chain.cpp` | 3 | Full SH-101 style signal chain. | OK |
| `Functional_BachMidi.cpp`| 3 | MIDI/polyphonic performance. | OK |

---

## 7. Global Parameter Registry (Contract)
*Tests MUST use these IDs for parameter modulation.*

| ID | Label | Target Tag | Description |
|----|-------|------------|-------------|
| 1 | `vcf_cutoff` | `VCF` | Filter frequency (Hz) |
| 2 | `vcf_res` | `VCF` | Resonance (0.0 - 1.0) |
| 3 | `vcf_env_amount` | `VCF` | Env -> Filter depth |
| 4 | `amp_attack` | `VCA` | VCA Attack (Seconds) |
| 5 | `amp_decay` | `VCA` | VCA Decay (Seconds) |
| 6 | `amp_sustain` | `VCA` | VCA Sustain (0.0 - 1.0) |
| 7 | `amp_release` | `VCA` | VCA Release (Seconds) |
| 11 | `sub_gain` | `VCO` | Sub-oscillator level |
| 12 | `saw_gain` | `VCO` | Sawtooth level |
| 13 | `pulse_gain` | `VCO` | Pulse level |
| 14 | `pulse_width` | `VCO` | Pulse Width (0.0 - 1.0) |
| 15 | `sine_gain` | `VCO` | Sine level |
| 16 | `triangle_gain` | `VCO` | Triangle level |

---

## 8. Minimum Viable Sound Recipes
*Use these C++ snippets to build valid test configurations.*

### Recipe 1: Raw Drone (Tier 1)
```cpp
set_param(engine, "pulse_gain", 1.0f); 
set_param(engine, "vcf_cutoff", 20000.0f);
set_param(engine, "amp_sustain", 1.0f);
engine_note_on(engine, 60, 1.0f);
```

### Recipe 2: Articulated Mono (Tier 2)
```cpp
set_param(engine, "saw_gain", 0.8f);    
set_param(engine, "amp_attack", 0.05f); 
engine_connect_mod(engine, MOD_SRC_ENVELOPE, MOD_TGT_AMPLITUDE, 1.0f);
```

---

## 9. Configuration Guide

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

## 10. Test Output Standards

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
