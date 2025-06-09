# CMake generated Testfile for 
# Source directory: /home/cris/Documents/ssAGI/taskd/external/cJSON
# Build directory: /home/cris/Documents/ssAGI/taskd/build/external/cJSON
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(cJSON_test "/home/cris/Documents/ssAGI/taskd/build/external/cJSON/cJSON_test")
set_tests_properties(cJSON_test PROPERTIES  _BACKTRACE_TRIPLES "/home/cris/Documents/ssAGI/taskd/external/cJSON/CMakeLists.txt;248;add_test;/home/cris/Documents/ssAGI/taskd/external/cJSON/CMakeLists.txt;0;")
subdirs("tests")
subdirs("fuzzing")
