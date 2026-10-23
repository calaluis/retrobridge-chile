# RetroBridge Chile — ESP-IDF project root.
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# D-12: the toolchain is pinned to an exact ESP-IDF release (see docs/toolchain.md).
set(RB_IDF_VERSION "6.1.0")
set(rb_found_idf "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}")
if(NOT rb_found_idf VERSION_EQUAL RB_IDF_VERSION)
    message(FATAL_ERROR "RetroBridge requires ESP-IDF v${RB_IDF_VERSION}, found v${rb_found_idf}")
endif()

# Build only main and the components it requires, not every ESP-IDF component.
idf_build_set_property(MINIMAL_BUILD ON)

project(retrobridge)
