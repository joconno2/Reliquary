# CMake generated Testfile for 
# Source directory: /home/jim/projects/Reliquary
# Build directory: /home/jim/projects/Reliquary/build-windows
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[DataIntegrity]=] "/home/jim/projects/Reliquary/build-windows/test_data.exe")
set_tests_properties([=[DataIntegrity]=] PROPERTIES  WORKING_DIRECTORY "/home/jim/projects/Reliquary" _BACKTRACE_TRIPLES "/home/jim/projects/Reliquary/CMakeLists.txt;92;add_test;/home/jim/projects/Reliquary/CMakeLists.txt;0;")
subdirs("_deps/json-build")
