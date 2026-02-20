# Building the Audio Engine

## Prerequisites

### CMake
CMake 3.20 or later is required.

**macOS:**
```bash
brew install cmake
```

**Linux (Fedora):**
```bash
sudo dnf install cmake
```

**Linux (Debian/Ubuntu):**
```bash
sudo apt-get install cmake
```

### Platform-Specific Libraries

**macOS:**
- CoreAudio and AudioToolbox frameworks are included with macOS (no installation needed)

**Linux:**
- ALSA development libraries:
  - Fedora: `sudo dnf install alsa-lib-devel`
  - Debian/Ubuntu: `sudo apt-get install libasound2-dev`

## Building

### Using CMake (Recommended)

```bash
cd cxx
mkdir -p build
cd build
cmake ..
make
```

The executable will be in `build/bin/audio_test`.

### Direct Compilation (Quick Test)

If CMake is not available, you can compile directly:

**macOS:**
```bash
cd cxx
mkdir -p bin
clang++ -std=c++20 -Wall -Wextra -DAUDIO_ENABLE_PROFILING=1 -I. main.cpp -o bin/audio_test -framework CoreAudio -framework AudioToolbox
```

**Linux:**
```bash
cd cxx
mkdir -p bin
g++ -std=c++20 -Wall -Wextra -DAUDIO_ENABLE_PROFILING=1 -I. main.cpp -o bin/audio_test -lasound
```

## Running

```bash
./bin/audio_test
```

## CMake Options

- `AUDIO_ENABLE_PROFILING`: Enable performance profiling (default: ON)
  ```bash
  cmake -DAUDIO_ENABLE_PROFILING=OFF ..
  ```
