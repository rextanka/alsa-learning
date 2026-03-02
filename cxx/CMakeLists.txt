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
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/routing)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/hal)

# Optional: Enable profiling (can be controlled via CMake)
option(AUDIO_ENABLE_PROFILING "Enable performance profiling" ON)
if(AUDIO_ENABLE_PROFILING)
    add_compile_definitions(AUDIO_ENABLE_PROFILING=1)
    message(STATUS "Performance profiling: ENABLED")
else()
    message(STATUS "Performance profiling: DISABLED")
endif()

# --- Core Audio Engine Library (Static) ---
set(ENGINE_SOURCES
    src/bridge/AudioBridge.cpp
    src/core/Voice.cpp
    src/core/VoiceManager.cpp
    src/core/MidiParser.cpp
    src/core/PatchStore.cpp
)

if(APPLE)
    list(APPEND ENGINE_SOURCES src/hal/coreaudio/CoreAudioDriver.cpp)
elseif(UNIX AND NOT APPLE)
    list(APPEND ENGINE_SOURCES src/hal/alsa/AlsaDriver.cpp)
endif()

add_library(audio_engine STATIC ${ENGINE_SOURCES})

target_include_directories(audio_engine PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal
)

target_compile_options(audio_engine PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -fvisibility=default
)

if(PLATFORM_LIBS)
    target_link_libraries(audio_engine PRIVATE ${PLATFORM_LIBS})
endif()

target_link_libraries(audio_engine PUBLIC nlohmann_json::nlohmann_json)

# --- Core Audio Engine Library (Shared) ---
add_library(audio_engine_shared SHARED ${ENGINE_SOURCES})

target_include_directories(audio_engine_shared PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal
)

target_compile_options(audio_engine_shared PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -fvisibility=default
)

if(PLATFORM_LIBS)
    target_link_libraries(audio_engine_shared PRIVATE ${PLATFORM_LIBS})
endif()

target_link_libraries(audio_engine_shared PUBLIC nlohmann_json::nlohmann_json)

# Ensure the shared library is named consistently across platforms
set_target_properties(audio_engine_shared PROPERTIES OUTPUT_NAME "audio_engine")

# --- Testing Infrastructure (Phase 11) ---

option(BUILD_TESTING "Build tests" ON)

if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    )

    FetchContent_Declare(
      json
      URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )

    # For Windows: prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest json)

    enable_testing()

    # --- Unit Tests (C++) ---
    add_executable(unit_tests
        tests/unit/test_oscillator.cpp
        tests/unit/test_envelope_stages.cpp
        tests/unit/test_voice_stress.cpp
        tests/unit/test_logger.cpp
        tests/unit/test_clock_logic.cpp
        tests/unit/test_modulation_matrix.cpp
    )
    target_include_directories(unit_tests PRIVATE tests/unit tests)
    target_link_libraries(unit_tests PRIVATE audio_engine gtest_main)
    add_test(NAME unit_tests COMMAND unit_tests)
    set_tests_properties(unit_tests PROPERTIES LABELS "unit")

    # --- Integration Tests (C API) ---
    add_executable(integration_tests
        tests/integration/test_bridge.cpp
    )
    # Note: We link against audio_engine but we ONLY include CInterface.h in the source
    target_include_directories(integration_tests PRIVATE tests)
    target_link_libraries(integration_tests PRIVATE audio_engine gtest_main)
    add_test(NAME integration_tests COMMAND integration_tests)

    # --- Functional Tests (Standalone) ---
    function(add_functional_test name source)
        add_executable(${name} tests/functional/${source})
        target_include_directories(${name} PRIVATE tests)
        target_link_libraries(${name} PRIVATE audio_engine)
    endfunction()

    function(add_functional_gtest name source)
        add_executable(${name} tests/functional/${source})
        target_include_directories(${name} PRIVATE tests)
        target_link_libraries(${name} PRIVATE audio_engine gtest_main)
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    add_functional_test(audio_check audio_check.cpp)
    add_functional_test(metronome_test metronome_test.cpp)
    add_functional_test(oscillator_integrity_test oscillator_integrity_test.cpp)
    add_functional_test(oscillator_baseline_test oscillator_baseline_test.cpp)
    add_functional_test(filter_tests filter_tests.cpp)
    add_functional_test(stereo_poly_test stereo_poly_test.cpp)
    add_functional_test(processor_check processor_check.cpp)
    add_functional_test(engine_tests engine_tests.cpp)
    add_functional_test(Phase10Tests Phase10Tests.cpp)
    add_functional_test(TimingValidation TimingValidation.cpp)
    
    add_functional_gtest(Functional_BachMidi Functional_BachMidi.cpp)
    add_functional_gtest(tremulant_tests test_tremulant_preset.cpp)
    add_functional_gtest(sh101_chain_tests test_sh101_chain.cpp)
    add_functional_gtest(juno_chorus_tests test_juno_chorus.cpp)
    add_functional_test(Functional_SH101_Live Functional_SH101_Live.cpp)

    add_test(NAME Functional_Phase10 COMMAND Phase10Tests)
    add_test(NAME Functional_Timing COMMAND TimingValidation)

    if(UNIX AND NOT APPLE)
        add_functional_test(AlsaCheck AlsaCheck.cpp)
        add_test(NAME Functional_AlsaCheck COMMAND AlsaCheck)
    endif()
endif()

message(STATUS "Build output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
