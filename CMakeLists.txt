cmake_minimum_required(VERSION 3.16)
# Force use of clang before project() so CMake configures with the correct compiler
find_program(CLANG_BIN clang REQUIRED)
set(CMAKE_C_COMPILER ${CLANG_BIN} CACHE STRING "C compiler" FORCE)

project(taskd C)

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Clang is required to build taskd")
endif()

if(CMAKE_C_COMPILER_VERSION VERSION_LESS "19")
    message(FATAL_ERROR "Clang 19 or newer is required; found ${CMAKE_C_COMPILER_VERSION}")
endif()

# Ensure all git submodules are pulled
execute_process(
    COMMAND git submodule update --init --recursive
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE GIT_SUBMOD_RESULT
)
if(NOT GIT_SUBMOD_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to update git submodules")
endif()

# Build submodules when available
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/cJSON/CMakeLists.txt)
    add_subdirectory(external/cJSON)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/xxHash/Makefile)
    add_library(xxhash STATIC external/xxHash/xxhash.c)
endif()


add_executable(taskd taskd.c)
if(TARGET cjson)
    target_link_libraries(taskd PRIVATE cjson)
    target_include_directories(taskd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external/cJSON)
    target_include_directories(xxhash PUBLIC external/xxHash)
    target_link_libraries(taskd PRIVATE xxhash)
endif()

