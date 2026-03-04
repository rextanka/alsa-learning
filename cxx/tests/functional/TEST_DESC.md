# Functional Test Guide (TEST_DESC.md)

This document serves as the "Rosetta Stone" for the engine's functional testing suite. It defines the categories of tests, the global parameter mappings, and modular recipes for regenerating or building new engine tests.

---

## 1. The Test Registry

The functional tests are located in `cxx/tests/functional/` and are categorized based on their verification target.

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
| `oscillator_integrity_test.cpp` | Verifies solo/mute behavior of individual oscillators. | Discrete oscillator tones. | Compliance OK |
| `graph_audit_test.cpp` | Bottom-up audit of the dynamic signal chain stages. | Step-by-step RMS verification.| **DRIFT DETECTED** (Uses hardcoded gain names) |
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

Use these C++ snippets with `CInterface.h` to build valid test configurations.

### Recipe 1: Raw Drone (Source -> Output)
*Minimalist configuration to verify signal flow.*
```cpp
EngineHandle engine = engine_create(48000);
set_param(engine, "pulse_gain", 1.0f); // ID 13
set_param(engine, "vcf_cutoff", 20000.0f); // Open Filter (ID 1)
set_param(engine, "amp_sustain", 1.0f); // Hold VCA (ID 6)
engine_note_on(engine, 60, 1.0f);
```

### Recipe 2: Articulated Mono (Source -> VCA -> Output)
*Verifies VCA envelope functionality.*
```cpp
set_param(engine, "saw_gain", 0.8f);    // ID 12
set_param(engine, "amp_attack", 0.05f); // ID 4
set_param(engine, "amp_decay", 0.2f);   // ID 5
set_param(engine, "amp_sustain", 0.5f); // ID 6
set_param(engine, "amp_release", 0.1f); // ID 7
engine_note_on(engine, 48, 1.0f);
// Wait...
engine_note_off(engine, 48);
```

### Recipe 3: Classic Subtractive (Source -> VCA -> Filter -> Output)
*The standard synthesizer path.*
```cpp
set_param(engine, "pulse_gain", 0.7f);
set_param(engine, "vcf_cutoff", 500.0f); // Low Cutoff
set_param(engine, "vcf_res", 0.8f);      // High Resonance
set_param(engine, "vcf_env_amount", 2.0f); // 2 octaves sweep
engine_note_on(engine, 36, 1.0f);
```

### Recipe 4: Modulated Path (LFO -> Target)
*Verifies the Modulation Matrix and `engine_connect_mod`.*
```cpp
// Create an LFO (Internal ID 100+)
int lfo_id = engine_create_processor(engine, PROC_LFO);
// Connect LFO (source) to Filter (target) via Param Cutoff (ID 1)
engine_connect_mod(engine, lfo_id, ALL_VOICES, PARAM_CUTOFF, 0.5f);
```

---

## 4. Configuration Guide

To maintain the **10ms MMA Latency Target**, all functional tests should be configured as follows:

- **Global Sample Rate**: `48000Hz` (Mandatory for accurate DSP ramps).
- **Buffer Size**: `512 samples`.
  - Latency calculation: $512 / 48000 \approx 10.6ms$.
- **Driver**: Use `AlsaDriver` on Linux and `CoreAudioDriver` on macOS via the `EngineHandle` abstraction.

---

## 5. Architectural Drift & Refactoring Notes

The following tests require refactoring to comply with the **Dynamic Node Container** vision:

1. **`graph_audit_test.cpp`**: 
   - *Drift*: Uses explicit `pulse_gain`, `saw_gain`, and `sub_gain` names which assume a fixed `SourceMixer` structure.
   - *Fix*: Transition to verifying node existence via the discovery API and setting parameters by Tag-based IDs.
2. **`processor_check.cpp`**:
   - *Drift*: Directly casts `Processor` to internal types.
   - *Fix*: Use the Bridge API (`set_param`) exclusively.
