cmake_minimum_required(VERSION 3.16)

# Force use of clang before project() so CMake configures with the correct compiler
find_program(CLANG_BIN clang REQUIRED)
set(CMAKE_C_COMPILER ${CLANG_BIN} CACHE STRING "C compiler" FORCE)

project(taskd C)

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Clang is required to build taskd")
endif()

if(CMAKE_C_COMPILER_VERSION VERSION_LESS "17")
    message(FATAL_ERROR "Clang 17 or newer is required; found ${CMAKE_C_COMPILER_VERSION}")
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

# Enable LTO and stripping
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s")

# Build submodules when available
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/cJSON/CMakeLists.txt)
    # Prevent cJSON from building tests and fuzzing targets
    set(ENABLE_CJSON_TEST OFF CACHE BOOL "Disable cJSON test build" FORCE)
    set(ENABLE_CJSON_UNINSTALL OFF CACHE BOOL "Disable uninstall target" FORCE)
    set(ENABLE_LOCALES OFF CACHE BOOL "Disable locales" FORCE)
    set(ENABLE_CJSON_UTILS OFF CACHE BOOL "Disable cJSON_utils" FORCE)
    add_subdirectory(external/cJSON)
    set_target_properties(cjson PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/xxHash/Makefile)
    add_library(xxhash STATIC external/xxHash/xxhash.c)
    set_target_properties(xxhash PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(xxhash PUBLIC external/xxHash)
endif()

# Statically linked executable
add_executable(taskd taskd.c state_machine.c)
add_executable(sm_test sm_test.c state_machine.c)
set_target_properties(taskd PROPERTIES LINK_SEARCH_START_STATIC OFF)
set_target_properties(taskd PROPERTIES LINK_SEARCH_END_STATIC OFF)
set_target_properties(sm_test PROPERTIES LINK_SEARCH_START_STATIC OFF)
set_target_properties(sm_test PROPERTIES LINK_SEARCH_END_STATIC OFF)

# Link libraries statically
add_link_options(-Wl,--gc-sections)
add_compile_options(-ffunction-sections -fdata-sections)
add_compile_options(-Oz)  # if using Clang
# target_link_options(taskd PRIVATE -static)

if(TARGET cjson)
    target_link_libraries(taskd PRIVATE cjson)
    target_link_libraries(sm_test PRIVATE cjson)
    target_include_directories(taskd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external/cJSON)
    target_include_directories(sm_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external/cJSON)
endif()

if(TARGET xxhash)
    target_link_libraries(taskd PRIVATE xxhash)
    target_link_libraries(sm_test PRIVATE xxhash)
endif()
