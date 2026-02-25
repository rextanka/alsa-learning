# CMake generated Testfile for 
# Source directory: /Users/nickthompson/src/cpp/audio/alsa-learning/cxx
# Build directory: /Users/nickthompson/src/cpp/audio/alsa-learning/cxx/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[unit_tests]=] "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/build/bin/unit_tests")
set_tests_properties([=[unit_tests]=] PROPERTIES  LABELS "unit" _BACKTRACE_TRIPLES "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;142;add_test;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;0;")
add_test([=[integration_tests]=] "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/build/bin/integration_tests")
set_tests_properties([=[integration_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;152;add_test;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;0;")
add_test([=[Functional_Phase10]=] "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/build/bin/Phase10Tests")
set_tests_properties([=[Functional_Phase10]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;170;add_test;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;0;")
add_test([=[Functional_Timing]=] "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/build/bin/TimingValidation")
set_tests_properties([=[Functional_Timing]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;171;add_test;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;0;")
subdirs("_deps/googletest-build")
