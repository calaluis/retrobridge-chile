# retrobridge_host — PC executable that serves D1 to an emulated Atari over NetSIO
# (Issue #32, E1b; ADR-016, decision D1). Builds the same pure components as the
# host tests, plus the PC adapters in src/ (UDP on loopback, PC clock, JSONL log).
#
#   cmake -S host -B build/retrobridge_host -G Ninja
#   cmake --build build/retrobridge_host
cmake_minimum_required(VERSION 3.22)
project(retrobridge_host LANGUAGES CXX)

set(rb_root ${CMAKE_CURRENT_LIST_DIR}/..)
include(${rb_root}/cmake/rb_build_flags.cmake)

add_subdirectory(${rb_root}/components/rb_core rb_core)
add_subdirectory(${rb_root}/components/rb_domain rb_domain)
add_subdirectory(${rb_root}/components/rb_application rb_application)
add_subdirectory(${rb_root}/components/rb_sio rb_sio)
add_subdirectory(${rb_root}/components/rb_file_storage rb_file_storage)
add_subdirectory(${rb_root}/components/rb_netsio rb_netsio)
add_subdirectory(${rb_root}/components/rb_net_tnfs rb_net_tnfs)
add_subdirectory(${rb_root}/components/rb_menu rb_menu)
find_package(Threads REQUIRED)

add_executable(retrobridge_host
    src/main.cpp
    src/file_identity.cpp
    src/udp_datagram_port.cpp
    src/jsonl_log.cpp
    src/tnfs_loader.cpp
    src/tnfs_link.cpp
    src/udp_tnfs_port.cpp)
target_link_libraries(retrobridge_host PRIVATE rb_menu rb_sio rb_file_storage rb_netsio rb_net_tnfs Threads::Threads)
rb_apply_build_flags(retrobridge_host)
if(WIN32)
    target_link_libraries(retrobridge_host PRIVATE ws2_32)
endif()

# Regression of the executable itself: --log must never write into the image
# (host/tests/test_log_collision.py). Run with: ctest --test-dir build/retrobridge_host
enable_testing()
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME retrobridge_host_log_collision
             COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_LIST_DIR}/tests/test_log_collision.py
                     $<TARGET_FILE:retrobridge_host>)
endif()
