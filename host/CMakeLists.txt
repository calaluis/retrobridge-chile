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

add_executable(retrobridge_host
    src/main.cpp
    src/file_identity.cpp
    src/udp_datagram_port.cpp
    src/jsonl_log.cpp)
target_link_libraries(retrobridge_host PRIVATE rb_sio rb_file_storage rb_netsio)
rb_apply_build_flags(retrobridge_host)
if(WIN32)
    target_link_libraries(retrobridge_host PRIVATE ws2_32)
endif()
