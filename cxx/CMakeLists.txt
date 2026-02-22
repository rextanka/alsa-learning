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
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Source files
set(AUDIO_SOURCES
    audio/InputSource.hpp
    audio/VoiceContext.hpp
    audio/PerformanceProfiler.hpp
    audio/Processor.hpp
)

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
    src/AudioBridge.cpp
    audio/Voice.cpp
    audio/VoiceManager.cpp
)

if(APPLE)
    target_sources(audio_engine PRIVATE hal/coreaudio/CoreAudioDriver.cpp)
elseif(UNIX AND NOT APPLE)
    target_sources(audio_engine PRIVATE hal/alsa/AlsaDriver.cpp)
endif()

target_include_directories(audio_engine PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/hal/include
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/envelope
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/filter
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/oscillator
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

# --- Test Executables ---

# 1. Metronome Validation Test (Phase 10)
add_executable(metronome_test tests/metronome_test.cpp)
target_link_libraries(metronome_test PRIVATE audio_engine)

# 2. Legacy Processor Check (formerly main.cpp)
add_executable(processor_check tests/processor_check.cpp)
target_link_libraries(processor_check PRIVATE audio_engine)

# 3. Basic Audio Driver Check
add_executable(audio_check tests/audio_check.cpp)
target_link_libraries(audio_check PRIVATE audio_engine)

# Other existing tests (update to link against audio_engine)
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
    add_executable(alsa_check tests/AlsaCheck.cpp hal/alsa/AlsaDriver.cpp)
    target_link_libraries(alsa_check ${PLATFORM_LIBS})
endif()

message(STATUS "Build output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
