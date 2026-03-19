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
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    if(NOT COREAUDIO_FRAMEWORK OR NOT AUDIOTOOLBOX_FRAMEWORK OR NOT COREFOUNDATION_FRAMEWORK)
        message(FATAL_ERROR "Failed to find CoreAudio or AudioToolbox frameworks")
    endif()
    set(PLATFORM_LIBS ${COREAUDIO_FRAMEWORK} ${AUDIOTOOLBOX_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
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
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/fx)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/routing)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/analysis)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/dynamics)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/fx)
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
    src/core/ProcessorRegistrations.cpp
    src/core/SmfParser.cpp
    src/core/MidiFilePlayer.cpp
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
        tests/unit/test_bus_headroom.cpp
        tests/unit/test_signal_chain.cpp
        tests/unit/test_voice_factory.cpp
        tests/unit/test_voice_groups.cpp
        tests/unit/test_lfo.cpp
        tests/unit/test_cv_utilities.cpp
        tests/unit/test_distortion_processor.cpp
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
        add_test(NAME ${name} COMMAND ${name}
                 WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    endfunction()

    add_functional_test(audio_check audio_check.cpp)
    add_functional_test(automated_osc_integrity automated_osc_integrity.cpp)
    add_functional_test(metronome_test metronome_test.cpp)
    add_functional_test(four_beeps_adsr four_beeps_adsr.cpp)
    add_functional_test(guitar_tuner_verify guitar_tuner_verify.cpp)
    add_functional_test(oscillator_baseline_test oscillator_baseline_test.cpp)
    add_functional_test(oscillator_integrity_test oscillator_integrity_test.cpp)
    add_functional_test(graph_audit_test graph_audit_test.cpp)
    add_functional_test(filter_sweep_test filter_sweep_test.cpp)
    add_functional_test(acid_live_tweak acid_live_tweak.cpp)
    add_functional_test(processor_check processor_check.cpp)
    add_functional_test(oscillator_drone_test oscillator_drone_test.cpp)
    add_functional_test(Phase10Tests Phase10Tests.cpp)
    add_functional_test(TimingValidation TimingValidation.cpp)
    
    add_functional_gtest(Functional_BachMidi Functional_BachMidi.cpp)
    add_functional_gtest(BachOrganTest BachOrganTest.cpp)
    add_functional_gtest(tremulant_tests test_tremulant_preset.cpp)
    add_functional_gtest(lfo_modulation_tests test_lfo_modulation.cpp)
    add_functional_gtest(sh101_chain_tests test_sh101_chain.cpp)
    add_functional_gtest(juno_chorus_tests test_juno_chorus.cpp)
    add_functional_gtest(stereo_poly_tests stereo_poly_test.cpp)
    add_functional_test(Functional_SH101_Live Functional_SH101_Live.cpp)
    add_functional_test(patch_sequence_test patch_sequence_test.cpp)
    add_functional_gtest(midi_file_playback_tests test_midi_file_playback.cpp)
    add_functional_gtest(harpsichord_patch_tests test_harpsichord_patch.cpp)
    add_functional_gtest(dog_whistle_patch_tests test_dog_whistle_patch.cpp)
    add_functional_gtest(sh_bass_patch_tests test_sh_bass_patch.cpp)
    add_functional_gtest(tb_bass_patch_tests test_tb_bass_patch.cpp)
    add_functional_gtest(cymbal_patch_tests test_cymbal_patch.cpp)
    add_functional_gtest(bowed_bass_patch_tests test_bowed_bass_patch.cpp)
    add_functional_gtest(brass_patch_tests test_brass_patch.cpp)
    add_functional_gtest(flute_patch_tests test_flute_patch.cpp)
    add_functional_gtest(juno_pad_patch_tests test_juno_pad_patch.cpp)
    add_functional_gtest(organ_drawbar_patch_tests test_organ_drawbar_patch.cpp)
    add_functional_gtest(percussion_noise_patch_tests test_percussion_noise_patch.cpp)
    add_functional_gtest(violin_vibrato_patch_tests test_violin_vibrato_patch.cpp)
    add_functional_gtest(bell_patch_tests test_bell_patch.cpp)
    add_functional_gtest(reverb_patch_tests test_reverb_patch.cpp)
    add_functional_gtest(wind_surf_patch_tests test_wind_surf_patch.cpp)
    add_functional_gtest(pizzicato_violin_patch_tests test_pizzicato_violin_patch.cpp)
    add_functional_gtest(group_strings_patch_tests test_group_strings_patch.cpp)
    add_functional_gtest(banjo_patch_tests test_banjo_patch.cpp)
    add_functional_gtest(glockenspiel_patch_tests test_glockenspiel_patch.cpp)
    add_functional_gtest(english_horn_patch_tests test_english_horn_patch.cpp)
    add_functional_gtest(wood_blocks_patch_tests test_wood_blocks_patch.cpp)
    add_functional_gtest(whistling_patch_tests test_whistling_patch.cpp)
    add_functional_gtest(clarinet_patch_tests test_clarinet_patch.cpp)
    add_functional_gtest(snare_drum_patch_tests test_snare_drum_patch.cpp)
    add_functional_gtest(bass_drum_patch_tests test_bass_drum_patch.cpp)
    add_functional_gtest(bongo_drums_patch_tests test_bongo_drums_patch.cpp)
    add_functional_gtest(gong_patch_tests test_gong_patch.cpp)
    add_functional_gtest(cow_bell_patch_tests test_cow_bell_patch.cpp)
    add_functional_gtest(rain_patch_tests test_rain_patch.cpp)
    add_functional_gtest(tom_tom_patch_tests test_tom_tom_patch.cpp)
    add_functional_gtest(thunder_patch_tests test_thunder_patch.cpp)
    add_functional_gtest(juno_strings_patch_tests test_juno_strings_patch.cpp)
    add_functional_gtest(acid_reverb_patch_tests test_acid_reverb_patch.cpp)
    add_functional_gtest(delay_lead_patch_tests test_delay_lead_patch.cpp)
    add_functional_gtest(gong_full_patch_tests test_gong_full_patch.cpp)
    add_functional_gtest(host_device_tests test_host_devices.cpp)
    add_functional_gtest(strings_postchain_tests test_strings_postchain.cpp)

    # Copy patch JSON files and MIDI test data to the binary output directory
    # so tests can be run directly from build/bin without extra arguments.
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/patches
         DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/midi
         DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

    add_test(NAME Functional_Phase10 COMMAND Phase10Tests)
    add_test(NAME Functional_Timing COMMAND TimingValidation)
    add_test(NAME Functional_OscIntegrity COMMAND automated_osc_integrity)
    add_test(NAME Functional_ProcessorFidelity COMMAND processor_check)

endif()

message(STATUS "Build output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")