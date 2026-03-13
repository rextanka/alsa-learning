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
