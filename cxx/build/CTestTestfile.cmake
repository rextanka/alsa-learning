# CMake generated Testfile for 
# Source directory: /home/nickt/src/alsa/alsa-learning/cxx
# Build directory: /home/nickt/src/alsa/alsa-learning/cxx/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[unit_tests]=] "/home/nickt/src/alsa/alsa-learning/cxx/build/bin/unit_tests")
set_tests_properties([=[unit_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/nickt/src/alsa/alsa-learning/cxx/CMakeLists.txt;139;add_test;/home/nickt/src/alsa/alsa-learning/cxx/CMakeLists.txt;0;")
add_test([=[integration_tests]=] "/home/nickt/src/alsa/alsa-learning/cxx/build/bin/integration_tests")
set_tests_properties([=[integration_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/nickt/src/alsa/alsa-learning/cxx/CMakeLists.txt;147;add_test;/home/nickt/src/alsa/alsa-learning/cxx/CMakeLists.txt;0;")
subdirs("_deps/googletest-build")
