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

# Main executable (processor + oscillator tests)
add_executable(audio_test 
    main.cpp 
    bridge/AudioBridge.cpp
    audio/Voice.cpp
    audio/VoiceManager.cpp
)

# Audible Engine Tests
add_executable(engine_tests 
    tests/EngineTests.cpp
    audio/Voice.cpp
    audio/VoiceManager.cpp
)

# HAL sources
if(APPLE)
    target_sources(audio_test PRIVATE hal/coreaudio/CoreAudioDriver.cpp)
    target_sources(engine_tests PRIVATE hal/coreaudio/CoreAudioDriver.cpp)
elseif(UNIX AND NOT APPLE)
    # target_sources(audio_test PRIVATE hal/alsa/AlsaDriver.cpp)
endif()

# Link platform-specific libraries
if(PLATFORM_LIBS)
    target_link_libraries(audio_test ${PLATFORM_LIBS})
    target_link_libraries(engine_tests ${PLATFORM_LIBS})
endif()

# Compiler options
target_compile_options(audio_test PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

message(STATUS "Build output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
