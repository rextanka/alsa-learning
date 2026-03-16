# Things to Optimise

This document tracks technical debt, performance bottlenecks, and areas for refinement in the Audio Engine.

## Testing & Diagnostics

### Zero-Crossing Counter
- **Current State:** Used in `graph_audit_test.cpp` for lightweight signal presence and rough frequency verification.
- **Limitation:** The zero-crossing method is susceptible to errors from complex harmonic content (like Pulse Width Modulation) and DC offsets. It is not suitable for high-precision pitch analysis.
- **Refinement:** High-precision pitch verification should always use the `DctProcessor` (as seen in `automated_osc_integrity.cpp`). The zero-crossing counter should be treated as an informational "signal health" probe only.
