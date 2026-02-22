# Building the Audio Engine

## Prerequisites

### Common
- **CMake** (3.20+)
- **C++20/23 Compiler** (Clang 15+, GCC 12+, or MSVC 19.30+)
- **GoogleTest** (Required for Phase 11+ tests)
  - Fedora: `sudo dnf install gtest-devel`
  - Ubuntu: `sudo apt-get install libgtest-dev`
  - macOS: `brew install googletest`

### Linux (Fedora/Ubuntu)
- **ALSA Development Libraries**
  - Fedora: `sudo dnf install alsa-lib-devel`
  - Ubuntu: `sudo apt-get install libasound2-dev`

### macOS
- **CoreAudio Frameworks** (Included with Xcode/Command Line Tools)

---

## Build Instructions

1. **Create Build Directory**
   ```bash
   mkdir -p cxx/build
   cd cxx/build
   ```

2. **Configure with CMake**
   ```bash
   cmake ..
   ```

3. **Compile**
   ```bash
   make
   ```

## Running Tests

All binaries are output to `cxx/build/bin/`.

- **Metronome Validation**:
  ```bash
  ./bin/metronome_test
  ```

- **Audio Driver Check**:
  ```bash
  ./bin/audio_check
  ```

- **GUnit Tests (Phase 11+)**:
  ```bash
  ./bin/unit_tests
  ```
