# Functional Test Guide (TEST_DESC.md)

This document defines the requirements for functional engine tests. All tests MUST be modular and follow the "Graph-First" design principle.

---

## 1. Modular Graph Initialization
Tests must explicitly initialize the graph according to their requirements. Do not use a "one-size-fits-all" lifecycle. Choose the appropriate Tier:

- **Tier 1 (Direct Path)**: `Oscillator -> Output`.
  - Required: `engine_start`, `set_param` (Gain).
- **Tier 2 (Modulated Path)**: `Oscillator -> Modulator -> Output`.
  - Required: Tier 1 requirements + `engine_connect_mod` (e.g., ADSR -> VCA).
- **Tier 3 (Complex Path)**: `Oscillator -> VCF -> VCA -> Output`.
  - Required: Tier 2 requirements + Filter/Resonance initialization.

---

## 2. Mandatory "Graph-Aware" Checklist
Before a test is executed, the developer (or Cline) must confirm:
1. **Graph Definition**: Is the signal path documented in the `PRINT_TEST_HEADER`?
2. **Lifecycle State**: Has `engine_start()` been called for this specific configuration?
3. **Connectivity**: If modulation is used, has `engine_connect_mod` been called to link the source to the target?
4. **Gain Stage**: Is the gain stage of every module in the graph explicitly initialized to a non-zero value?

---

## 3. Protocol for Cline (Context-Aware)
When Cline works on a test, it must use this logic:
1. **Identify Tier**: "What is the simplest possible graph for this test?"
2. **Declare Path**: Write the signal path in the test header.
3. **Explicit Patching**: If the path involves modulation, verify the connection exists in the code *before* debugging DSP values.
4. **Minimalism**: If the test is Tier 1, do not attempt to configure ADSR/Filter parameters.
