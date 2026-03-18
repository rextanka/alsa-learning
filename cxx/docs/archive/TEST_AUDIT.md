# Functional Test Audit Log
Last Performed: 2026-03-16 (updated post-audit 2026-03-17)

This document tracks the sanity and utility of functional tests following the protocol defined in TESTING.md.

> **Naming convention**: Test names in this table use CMake build target names (e.g. `sh101_chain_tests`). The corresponding source files are named `test_<name>.cpp` or `<name>.cpp` as listed in TESTING.md §5.

| Test Name | Status | Compliance | Sanity Result |
| :--- | :--- | :--- | :--- |
| `audio_check` | PASS | Adheres to Tier 1 protocol | Verified A440 tone, 2s duration |
| `automated_osc_integrity` | PASS | Adheres to Tier 2 protocol | Verified C4, A4, A5 within 1% tolerance |
| `BachOrganTest` | PASS | Adheres to Tier 3 protocol | Self-calibrated to hardware; verified tempo-accurate polyphonic MIDI playback |
| `filter_sweep_test` | PASS | Adheres to Tier 3 protocol | Verified Moog and Diode ladder resonant sweeps |
| `four_beeps_adsr` | PASS | Adheres to Tier 2 protocol | Verified 4 distinct ADSR-articulated pulse beeps |
| `Functional_BachMidi` | PASS | Adheres to Tier 3 protocol | Verified polyphonic organ performance (BWV 578 & 846) |
| `Functional_SH101_Live` | PASS | Adheres to Tier 3 protocol | Verified patch loading and low-frequency chromatic performance |
| `graph_audit_test` | PASS | Adheres to Tier 3 protocol | Verified signal presence (RMS > 0.001) and hardware health (Xruns) |
| `guitar_tuner_verify` | PASS | Adheres to Tier 1 protocol | Verified tuning for strings E2, A2, D3, G3, B3, E4 |
| `metronome_test` | PASS | Adheres to Tier 1 protocol | Verified precise 120 BPM clicks over 2 bars |
| `oscillator_baseline_test` | PASS | Adheres to Tier 2 protocol | Verified 10s of A4 Sine/ADSR cycling |
| `oscillator_drone_test` | PASS | Adheres to Tier 1 protocol | Verified continuous drones for Sine, Square, Saw, Triangle |
| `oscillator_integrity_test` | PASS | Adheres to Tier 2 protocol | Verified sequential isolated plucks for all 6 oscillator types including Wavetable and Sub |
| `Phase10Tests` | PASS | Adheres to Tier 2 protocol | Verified BPM clock precision and audible string-based note mapping |
| `sh101_chain_tests` | PASS | Adheres to Tier 3 protocol | Refactored for real-time audibility; verified SH-101 signal path |
| `processor_check` | PASS | Fidelity Audit | Verified oscillator frequency accuracy via hysteresis zero-crossing |
| `test_juno_chorus` | PASS | Stereo Verification | Verified stereo separation from mode-based chorus processing |
| `test_tremulant_preset` | PASS | Adheres to Tier 2 protocol | Rewritten for Phase 15A: verified LFO→Pitch via engine_set_lfo_* API; verified clear resets modulation |
| `lfo_modulation_tests` | PASS | Adheres to Tier 2 protocol | 8 tests: API error codes, vibrato block variance, cutoff LFO amplitude variation, clear-modulations reset |
| `TimingValidation` | PASS | Driver Stability | Verified callback jitter and sample-accurate clock drift |
| `SummingBus` | PASS | Unit Test Validation | Verified constant-power panning and 16-voice headroom |
| `patch_sequence_test` | PASS | Adheres to Tier 3 protocol | Verified four 8-bar musical phrases (SH-101, TB-303, Juno Pad, Drawbar Organ) |
| `stereo_poly_tests` | PASS | Stereo Verification | Verified L≠R for panned voices, L=R for centre-panned note |
| **OVERALL** | **PASS** | **100% GREEN** | **Functional and Unit test suites are fully verified (23 functional tests + 11 new LFO unit tests).** |

---

## Audit Items — Phase 15 (2026-03-16)

All HIGH and MEDIUM items resolved. One LOW item deferred to future feature work.

### Resolved

| ID | Priority | Resolution |
|----|----------|-----------|
| A1 | HIGH | `engine_save_patch` stub removed from `AudioBridge.cpp`; `PatchPersistence` test replaced with a comment noting deferral. |
| A2 | HIGH | `engine_save_patch` declaration removed from `CInterface.h`; `PatchStore` v1 save path left in place but not exposed via API. |
| A3 | MEDIUM | External integer-ID mod matrix (`engine_connect_mod`, `engine_create_processor`) deprecated in comments. New `engine_set_lfo_*` API (Phase 15A) is the canonical LFO path. Full removal deferred to Phase 16. |
| B1 | HIGH | `MoogLadderProcessor` and `DiodeLadderProcessor`: `resonance_cv` → `res_cv`. |
| B2 | HIGH | `MoogLadderProcessor` and `DiodeLadderProcessor`: `kybd_cv` port declaration added. |
| B3 | HIGH | `LfoProcessor`: `lfo_out` → `control_out`. |
| B4 | HIGH | `LfoProcessor`: `output_port_type()` override added returning `PORT_CONTROL`. |
| B5 | MEDIUM | `LfoProcessor`: `rate_cv` and `reset` input port declarations added. |
| C1 | HIGH | `VoiceManager` constructor: `VoiceFactory::createSH101()` replaced with `std::make_unique<Voice>(sample_rate)`. `#include "VoiceFactory.hpp"` removed. |
| D1 | HIGH | `MoogLadderProcessor::do_pull`: `snprintf` + `log_message` block removed entirely. |
| D2 | MEDIUM | `AdsrEnvelopeProcessor::gate_on`: `log_message("ADSR", "Gate On")` replaced with RT-safe `log_event("ADSR", 1.0f)`. |

### Deferred

| ID | Priority | Item | Deferred To |
|----|----------|------|-------------|
| B6 | LOW | `DrawbarOrganProcessor`: `percussion` and `percussion_decay` parameters not declared. | Phase 16+ — implement when percussion chiff feature lands. |
| A3-followup | LOW | `engine_connect_mod` / `engine_create_processor` / `engine_get_modulation_report` / `MOD_SRC_*` / `MOD_TGT_*` / `ALL_VOICES` — confirm removal is complete in implementation (BRIDGE_GUIDE.md §9 now marks these Removed per Phase 16 completion). | Verify at next implementation audit. |

---

## Spec Additions — 2026-03-17

The following modules were added to MODULE_DESC.md and PATCH_SPEC.md during Roland reference document gap analysis. No implementation exists yet; no tests are required until each module lands.

| Module | Source | Notes |
|--------|--------|-------|
| `ECHO_DELAY` — `mod_rate`, `mod_intensity` params | Roland Vol 2 §3-5, Fig 3-16 (Cymbal) | Extends existing planned spec; `cymbal.json` patch added |
| `REVERB` | Roland Recording §2-6, §3-5 | Global FX module; `time`, `pre_delay`, `damping`, `mix` params |
| `NOISE_GATE` | Roland Recording §4-5, Fig 4-4 (Boss NF-1) | Amplitude & Dynamics module; `threshold`, `attack`, `decay` params |
