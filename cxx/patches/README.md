# Patch Library

This directory contains the patch JSON files for the Musical Toolbox engine.
Each patch defines a signal chain (modules + port connections + parameters) that
the engine loads via `engine_load_patch()`.

## Important: Do not modify patches without updating tests

Every patch in this directory has a companion MIDI fixture in `midi/` and is
covered by two layers of automated testing:

1. **Smoke test** (`smoke_<name>` ctest entry) — loads the patch, drives it with
   `midi/<name>.mid`, and asserts non-silent output.  Runs in CI.
2. **Signal assertion test** (`test_<name>_patch.cpp`) — verifies patch-specific
   characteristics: percussive decay ratio, spectral centroid, attack envelope
   shape, stereo correlation, etc.

**If you change a patch JSON file, the corresponding tests may fail.**  This is
intentional — the tests encode the expected sonic behaviour of the patch.  When
you legitimately change a patch's sound (e.g. retuning, new topology), update
the companion test thresholds and MIDI fixture to match.

## Adding a new patch

1. Create `patches/<name>.json` with the voice chain.
2. Add a `mk_<name>()` function to `tools/gen_test_midi.py` and regenerate:
   ```
   python3 tools/gen_test_midi.py
   ```
3. Add `add_patch_smoke(<name>)` to `CMakeLists.txt`.
4. Add a `test_<name>_patch.cpp` GTest file for signal assertions (optional but
   recommended — see existing files for the standard pattern).
5. Update `midi/TEST_PATCHES_MIDI.md` with the new entry.

## Patch format

Patches are JSON v2 (v3 adds a top-level `post_chain` array for global
effects — see ARCH_PLAN.md §Phase 27B for the full specification).
`voice_mode` and `voice_count` fields are reserved but not yet implemented.

See `docs/PATCH_SPEC.md` for the complete format reference.
