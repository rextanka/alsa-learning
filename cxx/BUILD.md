# Building the Audio Engine

## Prerequisites

### Common
- **CMake** 3.20+
- **C++20 Compiler** — Clang 15+, GCC 12+, or MSVC 19.30+
- **GoogleTest** — downloaded automatically via `FetchContent` during configuration
- **nlohmann/json** — downloaded automatically via `FetchContent`

### Linux (Fedora / Ubuntu)
```bash
# Fedora
sudo dnf install alsa-lib-devel cmake

# Ubuntu / Debian
sudo apt-get install libasound2-dev cmake
```

### macOS
CoreAudio and AudioToolbox are included with Xcode Command Line Tools. No extra packages needed.

---

## Quick Start — Named Presets

`CMakePresets.json` defines four named configurations. This is the recommended way to build.

```bash
cd cxx

# Desktop debug build with all tests
cmake --preset desktop_full
cmake --build --preset desktop_full
ctest --preset desktop_full

# Desktop release build (no tests, no profiling)
cmake --preset desktop_release
cmake --build --preset desktop_release

# Raspberry Pi arm64 release build (requires PI_CROSS env var set)
cmake --preset pi_synth -DCMAKE_TOOLCHAIN_FILE=/path/to/arm64.cmake
cmake --build --preset pi_synth

# Raspberry Pi minimal (MinSizeRel)
cmake --preset pi_minimal -DCMAKE_TOOLCHAIN_FILE=/path/to/arm64.cmake
cmake --build --preset pi_minimal
```

Build output lands in `build/bin/` (desktop_full), `build_release/bin/`, `build_pi_synth/bin/`, or `build_pi_minimal/bin/` respectively.

---

## Manual Configuration (without presets)

```bash
cd cxx
mkdir -p build && cd build

# Configure
cmake .. -DBUILD_TESTING=ON -DAUDIO_ENABLE_PROFILING=ON

# Build
make -j$(nproc)

# Run tests
ctest --output-on-failure
```

---

## Running Tests

All test binaries are written to `build/bin/`:

```bash
# Full test suite via ctest
ctest --output-on-failure

# Run unit tests directly (verbose GTest output)
./build/bin/unit_tests

# Run integration tests
./build/bin/integration_tests

# Run a specific functional test
./build/bin/test_juno_pad_patch
```

### Test Tiers

| Directory | Purpose |
|---|---|
| `tests/unit/` | Internal C++ class tests — may use internal headers |
| `tests/integration/` | C API boundary tests |
| `tests/functional/` | Scenario tests — must use `CInterface.h` only |

---

## Legacy Functional Test Binaries

Some standalone programs are built but not registered with `ctest`. They exercise the engine with live audio output:

```bash
./build/bin/audio_check          # Basic driver stability
./build/bin/metronome_test       # Rhythmic click timing
./build/bin/processor_check      # Oscillator frequency fidelity
```

---

## Pre-Flight Checklist

Before creating a PR, confirm the following "Green Build" loop passes:

1. **Configure** — `cmake --preset desktop_full`
2. **Build** — `cmake --build --preset desktop_full`
3. **Test** — `ctest --preset desktop_full --output-on-failure`
4. **RT-Safety check** — any new code on the audio callback path must have no locks, allocations, or `printf`. Document as "RT-Safe" in comments.

---

## CMake Options

| Option | Default | Description |
|---|---|---|
| `BUILD_TESTING` | `ON` | Build GTest unit and functional tests |
| `AUDIO_ENABLE_PROFILING` | `ON` | Compile `AUDIO_ENABLE_PROFILING=1` define |

---

## Adding New Tests

- **Unit tests**: Add `.cpp` to `tests/unit/` and the `add_executable(unit_tests ...)` list in `CMakeLists.txt`.
- **Functional tests (standalone)**: Use `add_functional_test(name source)` in `CMakeLists.txt`.
- **Functional tests (GTest)**: Use `add_functional_gtest(name source)` in `CMakeLists.txt`.
- **Integration tests**: Add `.cpp` to `tests/integration/` and the `add_executable(integration_tests ...)` list.
