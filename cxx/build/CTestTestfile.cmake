# CMake generated Testfile for 
# Source directory: /Users/nickthompson/src/cpp/audio/alsa-learning/cxx
# Build directory: /Users/nickthompson/src/cpp/audio/alsa-learning/cxx/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[unit_tests]=] "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/build/bin/unit_tests")
set_tests_properties([=[unit_tests]=] PROPERTIES  LABELS "unit" _BACKTRACE_TRIPLES "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;140;add_test;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;0;")
add_test([=[integration_tests]=] "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/build/bin/integration_tests")
set_tests_properties([=[integration_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;150;add_test;/Users/nickthompson/src/cpp/audio/alsa-learning/cxx/CMakeLists.txt;0;")
subdirs("_deps/googletest-build")
