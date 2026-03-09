# CMake generated Testfile for 
# Source directory: /Users/nickthompson/src/cpp/audio/alsa-learning/cxx
# Build directory: /Users/nickthompson/src/cpp/audio/alsa-learning/cxx/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[unit_tests]=] "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/build/bin/unit_tests")
set_tests_properties([=[unit_tests]=] PROPERTIES  LABELS "unit" _BACKTRACE_TRIPLES "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;153;add_test;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;0;")
add_test([=[integration_tests]=] "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/build/bin/integration_tests")
set_tests_properties([=[integration_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;163;add_test;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;0;")
add_test([=[stereo_poly_test]=] "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/build/bin/stereo_poly_test")
set_tests_properties([=[stereo_poly_test]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;176;add_test;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;187;add_functional_gtest;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;0;")
add_test([=[Functional_BachMidi]=] "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/build/bin/Functional_BachMidi")
set_tests_properties([=[Functional_BachMidi]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;176;add_test;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;193;add_functional_gtest;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;0;")
add_test([=[tremulant_tests]=] "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/build/bin/tremulant_tests")
set_tests_properties([=[tremulant_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;176;add_test;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;194;add_functional_gtest;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;0;")
add_test([=[sh101_chain_tests]=] "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/build/bin/sh101_chain_tests")
set_tests_properties([=[sh101_chain_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;176;add_test;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;195;add_functional_gtest;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;0;")
add_test([=[juno_chorus_tests]=] "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/build/bin/juno_chorus_tests")
set_tests_properties([=[juno_chorus_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;176;add_test;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;196;add_functional_gtest;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;0;")
add_test([=[Functional_Phase10]=] "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/build/bin/Phase10Tests")
set_tests_properties([=[Functional_Phase10]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;199;add_test;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;0;")
add_test([=[Functional_Timing]=] "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/build/bin/TimingValidation")
set_tests_properties([=[Functional_Timing]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;200;add_test;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;0;")
add_test([=[Functional_OscIntegrity]=] "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/build/bin/automated_osc_integrity")
set_tests_properties([=[Functional_OscIntegrity]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;201;add_test;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;0;")
subdirs("_deps/googletest-build")
subdirs("_deps/json-build")
