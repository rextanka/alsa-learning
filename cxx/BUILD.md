# Building the Audio Engine

## Prerequisites

### Common
- **CMake** (3.20+)
- **C++20/23 Compiler** (Clang 15+, GCC 12+, or MSVC 19.30+)
- **GoogleTest**: Automatically downloaded via CMake `FetchContent` during configuration.

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
cd cxx/build
./bin/processor_check
```

## Pre-Flight Checklist

Before committing any changes, ensure you have completed the following "Green Build" loop:

1.  **Configure**: Ensure the build system is up to date.
    ```bash
    cd cxx
    mkdir -p build && cd build
    cmake ..
    ```
2.  **Compile**: Build all targets, including tests.
    ```bash
    make -j$(nproc)
    ```
3.  **Validate**: Run the full test suite.
    ```bash
    ctest --output-on-failure
    ```
    *Note: You can also run unit tests directly via `./bin/unit_tests` for more detailed GoogleTest output.*

4.  **Audio Thread Check**: Verify that no new code in `src/dsp` or `src/core` uses blocking calls or allocations. Use the `AudioLogger` for debugging in these paths.

2. **Configure with CMake**
   ```bash
   cmake .. -DBUILD_TESTING=ON
   ```

3. **Compile**
   ```bash
   make
   ```

## Contributor & Developer Workflow

### Building with Tests
To enable the test suite, use the `BUILD_TESTING` flag:

```bash
cd cxx
mkdir build && cd build
cmake .. -DBUILD_TESTING=ON
make
```

### Running Tests
We use `ctest` (bundled with CMake) to manage and run tests:

```bash
# Run all tests with output on failure
ctest --output-on-failure

# Run a specific test target
./bin/unit_tests
./bin/integration_tests
```

### Adding New Tests
- **Unit Tests**: Place in `cxx/tests/unit/`. Use these for testing internal C++ classes, private members, and complex DSP logic.
- **Integration Tests**: Place in `cxx/tests/integration/`. These should strictly use `CInterface.h` to simulate how an external developer uses the library.
- **Update CMake**: Add your new file to the `add_executable` list for `unit_tests` or `integration_tests` in `cxx/CMakeLists.txt`.

## Legacy Validation

All binaries are output to `cxx/build/bin/`.

- **Metronome Validation**:
  ```bash
  ./bin/metronome_test
  ```

- **Audio Driver Check**:
  ```bash
  ./bin/audio_check
  ```
