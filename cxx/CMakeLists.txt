cmake_minimum_required(VERSION 3.20)
project(AudioEngine VERSION 1.0.0 LANGUAGES CXX)

# Set C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directory for binaries (per .cursorrules)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Platform detection and library linking
if(APPLE)
    # macOS: Link CoreAudio and AudioToolbox
    find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
    if(NOT COREAUDIO_FRAMEWORK OR NOT AUDIOTOOLBOX_FRAMEWORK)
        message(FATAL_ERROR "Failed to find CoreAudio or AudioToolbox frameworks")
    endif()
    set(PLATFORM_LIBS ${COREAUDIO_FRAMEWORK} ${AUDIOTOOLBOX_FRAMEWORK})
    message(STATUS "Platform: macOS - Linking CoreAudio and AudioToolbox")
elseif(UNIX AND NOT APPLE)
    # Linux: Link ALSA
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ALSA REQUIRED alsa)
    if(NOT ALSA_FOUND)
        message(FATAL_ERROR "ALSA (libasound) not found. Install with: sudo dnf install alsa-lib-devel (Fedora) or sudo apt-get install libasound2-dev (Debian/Ubuntu)")
    endif()
    set(PLATFORM_LIBS ${ALSA_LIBRARIES})
    include_directories(${ALSA_INCLUDE_DIRS})
    message(STATUS "Platform: Linux - Linking ALSA (asound)")
elseif(WIN32)
    # Windows: WASAPI (future)
    message(STATUS "Platform: Windows - WASAPI support not yet implemented")
    set(PLATFORM_LIBS "")
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/core)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/dsp)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/oscillator)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/envelope)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/filter)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/hal)

# Optional: Enable profiling (can be controlled via CMake)
option(AUDIO_ENABLE_PROFILING "Enable performance profiling" ON)
if(AUDIO_ENABLE_PROFILING)
    add_compile_definitions(AUDIO_ENABLE_PROFILING=1)
    message(STATUS "Performance profiling: ENABLED")
else()
    message(STATUS "Performance profiling: DISABLED")
endif()

# --- Core Audio Engine Library ---
add_library(audio_engine STATIC
    src/bridge/AudioBridge.cpp
    src/core/Voice.cpp
    src/core/VoiceManager.cpp
)

if(APPLE)
    target_sources(audio_engine PRIVATE src/hal/coreaudio/CoreAudioDriver.cpp)
elseif(UNIX AND NOT APPLE)
    target_sources(audio_engine PRIVATE src/hal/alsa/AlsaDriver.cpp)
endif()

target_include_directories(audio_engine PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal
)

target_compile_options(audio_engine PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -fvisibility=default # Ensure RTTI consistency
)

# Link platform-specific libraries to the engine library
if(PLATFORM_LIBS)
    target_link_libraries(audio_engine ${PLATFORM_LIBS})
endif()

# --- Testing Infrastructure (Phase 11) ---

option(BUILD_TESTING "Build tests" ON)

if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    )
    # For Windows: prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    # --- Unit Tests (C++) ---
    add_executable(unit_tests
        tests/unit/test_oscillator.cpp
        tests/unit/test_logger.cpp
        tests/unit/test_clock_logic.cpp
        tests/unit/test_voice_stress.cpp
        tests/unit/test_envelope_stages.cpp
    )
    target_link_libraries(unit_tests PRIVATE audio_engine gtest_main)
    add_test(NAME unit_tests COMMAND unit_tests)

    # --- Integration Tests (C API) ---
    add_executable(integration_tests
        tests/integration/test_bridge.cpp
    )
    # Note: We link against audio_engine but we ONLY include CInterface.h in the source
    target_link_libraries(integration_tests PRIVATE audio_engine gtest_main)
    add_test(NAME integration_tests COMMAND integration_tests)
endif()

# --- Legacy Test Executables ---

# 1. Metronome Validation Test (Phase 10)
add_executable(metronome_test tests/metronome_test.cpp)
target_link_libraries(metronome_test PRIVATE audio_engine)

# 2. Legacy Processor Check (formerly main.cpp)
add_executable(processor_check tests/processor_check.cpp)
target_link_libraries(processor_check PRIVATE audio_engine)

# 3. Basic Audio Driver Check
add_executable(audio_check tests/audio_check.cpp)
target_link_libraries(audio_check PRIVATE audio_engine)

# Other existing tests
add_executable(engine_tests tests/EngineTests.cpp)
target_link_libraries(engine_tests PRIVATE audio_engine)

add_executable(filter_tests tests/FilterTests.cpp)
target_link_libraries(filter_tests PRIVATE audio_engine)

add_executable(stereo_poly_test tests/StereoPolyTest.cpp)
target_link_libraries(stereo_poly_test PRIVATE audio_engine)

add_executable(phase10_tests tests/Phase10Tests.cpp)
target_link_libraries(phase10_tests PRIVATE audio_engine)

add_executable(timing_validation tests/TimingValidation.cpp)
target_link_libraries(timing_validation PRIVATE audio_engine)

# ALSA Check utility (if UNIX)
if(UNIX AND NOT APPLE)
    add_executable(alsa_check tests/AlsaCheck.cpp src/hal/alsa/AlsaDriver.cpp)
    target_link_libraries(alsa_check ${PLATFORM_LIBS})
endif()

message(STATUS "Build output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
