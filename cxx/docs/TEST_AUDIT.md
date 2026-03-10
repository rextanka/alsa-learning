# Functional Test Audit Log
Last Performed: 2026-03-10

This document tracks the sanity and utility of functional tests following the protocol defined in TEST_DESC.md.

| Test Name | Status | Compliance | Sanity Result |
| :--- | :--- | :--- | :--- |
| `audio_check` | PASS | Adheres to Tier 1 protocol | Verified A440 tone, 2s duration |
| `automated_osc_integrity` | PASS | Adheres to Tier 2 protocol | Verified C4, A4, A5 within 1% tolerance |
| `BachOrganTest` | PASS | Adheres to Tier 3 protocol | Self-calibrated to hardware; verified tempo-accurate polyphonic MIDI playback |
| `filter_sweep_test` | PASS | Adheres to Tier 3 protocol | Verified Moog and Diode ladder resonant sweeps |
| `four_beeps_adsr` | PASS | Adheres to Tier 2 protocol | Verified 4 distinct ADSR-articulated pulse beeps |
