# CMake generated Testfile for 
# Source directory: /home/jim/Reliquary
# Build directory: /home/jim/Reliquary/build-windows
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[DataIntegrity]=] "/home/jim/Reliquary/build-windows/test_data.exe")
set_tests_properties([=[DataIntegrity]=] PROPERTIES  WORKING_DIRECTORY "/home/jim/Reliquary" _BACKTRACE_TRIPLES "/home/jim/Reliquary/CMakeLists.txt;92;add_test;/home/jim/Reliquary/CMakeLists.txt;0;")
subdirs("_deps/json-build")
