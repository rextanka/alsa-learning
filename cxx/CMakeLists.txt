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
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/io)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/routing)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/analysis)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/dynamics)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/fx)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/hal)

# Optional: Enable profiling (can be controlled via CMake)
option(AUDIO_ENABLE_PROFILING "Enable performance profiling" ON)

# Optional: Enable libsndfile external codec libs (ogg/vorbis, FLAC, Opus, MPEG).
# Requires the corresponding system libraries to be installed — see docs/DEPENDENCIES.md.
option(AUDIO_ENGINE_SNDFILE_EXTERNAL_LIBS "Enable libsndfile optional format support (FLAC, Ogg/Vorbis, Opus, MPEG)" OFF)
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
    src/core/SmfParser.cpp
    src/core/MidiFilePlayer.cpp
    src/core/ProcessorRegistrations.cpp
    # Per-processor .cpp files: each registers itself at static-init time
    # Filter
    src/dsp/filter/VcfBase.cpp
    src/dsp/filter/LadderVcfBase.cpp
    src/dsp/filter/MoogLadderProcessor.cpp
    src/dsp/filter/DiodeLadderProcessor.cpp
    src/dsp/filter/CemFilterProcessor.cpp
    src/dsp/filter/Ms20FilterProcessor.cpp
    src/dsp/filter/HighPassFilterProcessor.cpp
    src/dsp/filter/BandPassFilterProcessor.cpp
    # Envelope
    src/dsp/envelope/AdsrEnvelopeProcessor.cpp
    src/dsp/envelope/ADEnvelopeProcessor.cpp
    # Oscillator
    src/dsp/oscillator/LfoProcessor.cpp
    src/dsp/oscillator/WhiteNoiseProcessor.cpp
    # Dynamics
    src/dsp/dynamics/NoiseGateProcessor.cpp
    src/dsp/dynamics/EnvelopeFollowerProcessor.cpp
    # FX
    src/dsp/fx/EchoDelayProcessor.cpp
    src/dsp/fx/FreeverbProcessor.cpp
    src/dsp/fx/FdnReverbProcessor.cpp
    src/dsp/fx/PhaserProcessor.cpp
    src/dsp/fx/JunoChorus.cpp
    src/dsp/fx/DistortionProcessor.cpp
    # Routing
    src/dsp/routing/CompositeGenerator.cpp
    src/dsp/routing/DrawbarOrganProcessor.cpp
    src/dsp/routing/RingModProcessor.cpp
    src/dsp/routing/AudioMixerProcessor.cpp
    src/dsp/routing/AudioSplitterProcessor.cpp
    src/dsp/routing/CvMixerProcessor.cpp
    src/dsp/routing/CvSplitterProcessor.cpp
    src/dsp/routing/CvScalerProcessor.cpp
    src/dsp/routing/InverterProcessor.cpp
    src/dsp/routing/MathsProcessor.cpp
    src/dsp/routing/GateDelayProcessor.cpp
    src/dsp/routing/SampleHoldProcessor.cpp
    # I/O (Phase 27C)
    src/dsp/io/AudioOutputProcessor.cpp
    src/dsp/io/AudioInputProcessor.cpp
    src/dsp/io/AudioFileReaderProcessor.cpp
    src/dsp/io/AudioFileWriterProcessor.cpp
    # Root DSP
    src/dsp/VcaProcessor.cpp
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

if(NOT MSVC)
    target_compile_options(audio_engine PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -fvisibility=default
    )
endif()

if(PLATFORM_LIBS)
    target_link_libraries(audio_engine PRIVATE ${PLATFORM_LIBS})
endif()

target_link_libraries(audio_engine PUBLIC nlohmann_json::nlohmann_json)
target_link_libraries(audio_engine PUBLIC SndFile::sndfile SampleRate::samplerate)

# --- Core Audio Engine Library (Shared) ---
add_library(audio_engine_shared SHARED ${ENGINE_SOURCES})

target_include_directories(audio_engine_shared PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal
)

if(NOT MSVC)
    target_compile_options(audio_engine_shared PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -fvisibility=default
    )
endif()

if(PLATFORM_LIBS)
    target_link_libraries(audio_engine_shared PRIVATE ${PLATFORM_LIBS})
endif()

target_link_libraries(audio_engine_shared PUBLIC nlohmann_json::nlohmann_json)
target_link_libraries(audio_engine_shared PUBLIC SndFile::sndfile SampleRate::samplerate)

# Ensure the shared library is named consistently across platforms
set_target_properties(audio_engine_shared PROPERTIES OUTPUT_NAME "audio_engine")

# --- Engine Runtime Dependencies (always required) ---
# nlohmann/json, libsndfile, libsamplerate are production deps of the engine library,
# not test-only. Placed outside if(BUILD_TESTING) so the engine builds without tests.

include(FetchContent)

FetchContent_Declare(
  json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)

FetchContent_Declare(
  libsndfile
  GIT_REPOSITORY https://github.com/libsndfile/libsndfile.git
  GIT_TAG        1.2.2
  EXCLUDE_FROM_ALL  # CMake 3.28+: don't add libsndfile's tests to our CTest tree
)
set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
if(AUDIO_ENGINE_SNDFILE_EXTERNAL_LIBS)
    set(ENABLE_EXTERNAL_LIBS ON CACHE BOOL "" FORCE)
else()
    set(ENABLE_EXTERNAL_LIBS OFF CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
  libsamplerate
  GIT_REPOSITORY https://github.com/libsndfile/libsamplerate.git
  GIT_TAG        0.2.2
)
set(LIBSAMPLERATE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LIBSAMPLERATE_TESTS    OFF CACHE BOOL "" FORCE)

# CMAKE_POLICY_VERSION_MINIMUM (CMake 4.x): allow subprojects with cmake_minimum_required
# versions < 3.5 (libsndfile 1.2.2 declares 3.1..3.18, which CMake 4.x would otherwise reject).
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

option(BUILD_TESTING "Build tests" ON)
FetchContent_MakeAvailable(json libsndfile libsamplerate)

# libsndfile 1.2.2 registers ~143 internal tests via enable_testing()/add_test() in its
# CMakeLists.txt.  These appear as "Not Run" in our CTest tree because we don't build
# libsndfile's test executables.  CMake writes CTestTestfile.cmake during the generate
# phase (after all configure-time add_test() calls), so we can't suppress it during
# configure.  Use an ALL custom target to overwrite the file immediately after generate
# (i.e. after cmake --build, before ctest runs).
add_custom_target(suppress_libsndfile_tests ALL
    COMMAND ${CMAKE_COMMAND} -E echo
        "# libsndfile internal tests suppressed — executables not built by this project."
        > "${libsndfile_BINARY_DIR}/CTestTestfile.cmake"
    COMMENT "Suppressing libsndfile internal tests from CTest"
    VERBATIM
)

# --- Testing Infrastructure (Phase 11) ---

if(BUILD_TESTING)
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
        tests/unit/test_module_registry.cpp
    )
    target_include_directories(unit_tests PRIVATE tests/unit tests)
    target_link_libraries(unit_tests PRIVATE audio_engine gtest_main)
    add_test(NAME unit_tests COMMAND unit_tests)
    set_tests_properties(unit_tests PROPERTIES LABELS "unit")

    # --- Integration Tests (C API) ---
    add_executable(integration_tests
        tests/integration/test_bridge.cpp
        tests/integration/test_module_introspection.cpp
        tests/integration/test_patch_serialization.cpp
        tests/integration/test_tempo_sync.cpp
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
    add_functional_gtest(tremulant_tests test_tremulant_preset.cpp)
    add_functional_gtest(lfo_modulation_tests test_lfo_modulation.cpp)
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
    add_functional_gtest(brass_patch_tests    test_brass_patch.cpp)
    add_functional_gtest(trumpet_patch_tests  test_trumpet_patch.cpp)
    add_functional_gtest(trombone_patch_tests test_trombone_patch.cpp)
    add_functional_gtest(tuba_patch_tests     test_tuba_patch.cpp)
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
    add_functional_gtest(oboe_patch_tests test_oboe_patch.cpp)
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

    # ---------------------------------------------------------------------------
    # patch_test — generic patch + MIDI driver (audible playback + --smoke mode)
    # ---------------------------------------------------------------------------
    add_executable(patch_test tests/functional/patch_test.cpp)
    target_include_directories(patch_test PRIVATE tests)
    target_link_libraries(patch_test PRIVATE audio_engine nlohmann_json::nlohmann_json)

    # ---------------------------------------------------------------------------
    # Demos — standalone audible programs, not part of ctest
    # ---------------------------------------------------------------------------
    add_executable(midi_player demos/midi_player.cpp)
    target_include_directories(midi_player PRIVATE tests)
    target_link_libraries(midi_player PRIVATE audio_engine nlohmann_json::nlohmann_json)
    add_dependencies(midi_player sync_patch_fixtures sync_midi_fixtures)

    # Copy assets/midi (static assets) at configure time — these are stable.
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets/midi
         DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

    # patches/ and midi/ change during development; use build-time custom targets
    # so 'cmake --build' always re-syncs the files without needing re-configure.
    add_custom_target(sync_patch_fixtures ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/patches
                ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/patches
        COMMENT "Syncing patches/ to build output"
        VERBATIM)

    add_custom_target(sync_midi_fixtures ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/midi
                ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/midi
        COMMENT "Syncing midi/ fixtures to build output"
        VERBATIM)

    add_dependencies(patch_test sync_patch_fixtures sync_midi_fixtures)

    # ---------------------------------------------------------------------------
    # Patch smoke tests — offline render, no audio hardware, one per patch
    # ---------------------------------------------------------------------------
    function(add_patch_smoke name)
        add_test(NAME smoke_${name}
                 COMMAND patch_test --smoke
                         --patch patches/${name}.json
                         --midi  midi/${name}.mid
                 WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
    endfunction()

    add_patch_smoke(acid_reverb)
    add_patch_smoke(banjo)
    add_patch_smoke(bass_drum)
    add_patch_smoke(bell)
    add_patch_smoke(bongo_drums)
    add_patch_smoke(bowed_bass)
    add_patch_smoke(brass)
    add_patch_smoke(clarinet)
    add_patch_smoke(cow_bell)
    add_patch_smoke(cymbal)
    add_patch_smoke(delay_lead)
    add_patch_smoke(dog_whistle)
    add_patch_smoke(english_horn)
    add_patch_smoke(flute)
    add_patch_smoke(glockenspiel)
    add_patch_smoke(gong)
    add_patch_smoke(gong_full)
    add_patch_smoke(group_strings)
    add_patch_smoke(harpsichord)
    add_patch_smoke(juno_pad)
    add_patch_smoke(juno_strings)
    add_patch_smoke(organ_drawbar)
    add_patch_smoke(percussion_noise)
    add_patch_smoke(pizzicato_violin)
    add_patch_smoke(rain)
    add_patch_smoke(reverb_pad)
    add_patch_smoke(sh_bass)
    add_patch_smoke(snare_drum)
    add_patch_smoke(strings_chorus_reverb)
    add_patch_smoke(tb_bass)
    add_patch_smoke(thunder)
    add_patch_smoke(tom_tom)
    add_patch_smoke(violin_vibrato)
    add_patch_smoke(whistling)
    add_patch_smoke(wind_surf)
    add_patch_smoke(wood_blocks)

    add_test(NAME Functional_Phase10 COMMAND Phase10Tests)
    add_test(NAME Functional_Timing COMMAND TimingValidation)
    add_test(NAME Functional_OscIntegrity COMMAND automated_osc_integrity)
    add_test(NAME Functional_ProcessorFidelity COMMAND processor_check)

endif()

message(STATUS "Build output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")