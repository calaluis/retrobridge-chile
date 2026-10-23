# Host test build (ADR-010): CMake + CTest + Unity, without ESP-IDF.
# Builds the pure components with the same flags as the firmware.
#
#   cmake -S test/host -B build/host -G Ninja [-DRB_SANITIZE=ON]
#   cmake --build build/host
#   ctest --test-dir build/host --output-on-failure
cmake_minimum_required(VERSION 3.22)
project(retrobridge_host_tests LANGUAGES C CXX)

option(RB_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(RB_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

set(rb_root ${CMAKE_CURRENT_LIST_DIR}/../..)
include(${rb_root}/cmake/rb_build_flags.cmake)

add_subdirectory(${rb_root}/components/rb_core rb_core)
add_subdirectory(${rb_root}/components/rb_domain rb_domain)
add_subdirectory(${rb_root}/components/rb_application rb_application)
add_subdirectory(${rb_root}/components/rb_sio rb_sio)

# Unity v2.7.0, vendored (D-9). Third-party code: built with its own defaults.
add_library(unity STATIC unity/src/unity.c)
target_include_directories(unity PUBLIC unity/src)

enable_testing()

function(rb_add_host_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE unity rb_sio)
    rb_apply_build_flags(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

rb_add_host_test(test_toolchain toolchain/test_toolchain.cpp)
