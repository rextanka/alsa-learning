# External Dependencies

All dependencies are pulled in automatically via CMake `FetchContent` during configuration. No manual installation is required for normal development builds.

See `ARCH_PLAN.md §External Dependency Policy` for the criteria that govern accepting new dependencies.

---

## nlohmann/json

| | |
|---|---|
| **Purpose** | Patch serialisation — JSON parse and emit for `engine_get_patch_json`, `engine_load_patch_json`, `engine_load_patch`, `engine_save_patch` |
| **Version** | 3.11.3 (pinned via `FetchContent_Declare` SHA) |
| **License** | MIT |
| **Homepage** | https://github.com/nlohmann/json |
| **Added** | Phase 27B |

**What breaks without it**: The entire patch serialisation API (`engine_get_patch_json`, `engine_load_patch_json`, `engine_save_patch`) fails to compile. The bridge layer uses `nlohmann::json` directly — there is no abstraction layer around it.

**macOS / Linux**: Pulled from source via `FetchContent` — no system package needed.
**Windows**: Same. `FetchContent` builds it header-only; no compiler differences.

---

## libsndfile

| | |
|---|---|
| **Purpose** | `AUDIO_FILE_READER` and `AUDIO_FILE_WRITER` processors — reading and writing WAV and AIFF files |
| **Version** | 1.2.2 (pinned via `FetchContent_Declare` SHA) |
| **License** | LGPL-2.1 |
| **Homepage** | https://libsndfile.github.io/libsndfile/ |
| **Added** | Phase 27C |

**Supported formats**: WAV (PCM 16/24/32, float 32/64), AIFF (PCM 16/24/32, float 32). No compressed formats (MP3, AAC, OGG) — these are explicitly out of scope. See `LIBRARY_USER_MANUAL.md §Audio File I/O` for the full format table.

**What breaks without it**: `AUDIO_FILE_READER` and `AUDIO_FILE_WRITER` fail to register. The rest of the engine is unaffected.

**macOS / Linux**: `FetchContent` builds from source. No system package needed (though `brew install libsndfile` / `dnf install libsndfile-devel` work as fallback if `AUDIO_ENGINE_FETCH_SNDFILE=OFF`).
**Windows**: `FetchContent` builds from source using MSVC or MinGW. The library has first-class CMake support on Windows.

**Optional format support** (ogg/vorbis, FLAC, Opus, MPEG):

By default the build uses `ENABLE_EXTERNAL_LIBS OFF` which gives WAV and AIFF only. To enable additional formats, set the option before CMake configuration:

```bash
cmake -B build -DAUDIO_ENGINE_SNDFILE_EXTERNAL_LIBS=ON
```

This requires the corresponding system libraries to be installed:

| Format | Fedora | macOS (Homebrew) | Ubuntu/Debian |
|--------|--------|------------------|---------------|
| FLAC | `dnf install flac-devel` | `brew install flac` | `apt install libflac-dev` |
| Ogg/Vorbis | `dnf install libvorbis-devel` | `brew install libvorbis` | `apt install libvorbis-dev` |
| Opus | `dnf install opus-devel` | `brew install opus` | `apt install libopus-dev` |
| MPEG (read only) | `dnf install lame-devel mpg123-devel` | `brew install lame mpg123` | `apt install libmp3lame-dev libmpg123-dev` |

Note: these are read-only inputs for `AUDIO_FILE_READER`. `AUDIO_FILE_WRITER` always writes WAV (float 32) regardless of input format.

---

## libsamplerate

| | |
|---|---|
| **Purpose** | Sample-rate conversion in `AUDIO_FILE_READER` — converts file sample rate to engine sample rate off the audio thread |
| **Version** | 0.2.2 (pinned via `FetchContent_Declare` SHA) |
| **License** | BSD-2-Clause |
| **Homepage** | https://libsndfile.github.io/libsamplerate/ |
| **Added** | Phase 27C |

**What breaks without it**: `AUDIO_FILE_READER` fails to compile. Files whose sample rate matches the engine rate work without any SRC operation at runtime, but the SRC path is always compiled in.

**macOS / Linux**: `FetchContent` builds from source.
**Windows**: Same. The library ships a clean CMakeLists.txt with MSVC support.

---

## GoogleTest

| | |
|---|---|
| **Purpose** | Unit and integration test framework (`gtest`, `gmock`) |
| **Version** | 1.14.0 (pinned via `FetchContent_Declare` SHA) |
| **License** | BSD-3-Clause |
| **Homepage** | https://github.com/google/googletest |
| **Added** | Phase 1 |

**What breaks without it**: Test executables (`unit_tests`, `integration_tests`, `patch_test`, `midi_test`) fail to configure. The `audio_engine` and `audio_engine_shared` libraries themselves are unaffected.

**macOS / Linux / Windows**: `FetchContent` handles everything.

---

## Adding a New Dependency

Before adding, verify all four criteria in `ARCH_PLAN.md §External Dependency Policy`. Then:

1. Add `FetchContent_Declare` + `FetchContent_MakeAvailable` in `CMakeLists.txt`.
2. Add an entry to this file with purpose, version, license, homepage, and "what breaks without it".
3. Add the approved dependency row to the table in `ARCH_PLAN.md §External Dependency Policy`.
4. Update `BUILD.md §Prerequisites` if any system package is needed as a fallback.
