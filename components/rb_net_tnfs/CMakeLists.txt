# rb_net_tnfs — read-only TNFS client (menu-tnfs, ADR-012): codec and the
# session client over rb_netsio's DatagramPort.
# Pure C++17: no sockets, ESP-IDF, FreeRTOS or hardware libraries (ADR-002).
# The same file builds as an ESP-IDF component and as a plain static library
# for the host tests. The firmware does not require it (MINIMAL_BUILD, D3).
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rb_build_flags.cmake)

set(rb_srcs
    "src/tnfs_codec.cpp"
    "src/tnfs_client.cpp")

if(ESP_PLATFORM)
    idf_component_register(SRCS ${rb_srcs} INCLUDE_DIRS "include" REQUIRES rb_core rb_netsio)
    rb_apply_build_flags(${COMPONENT_LIB})
else()
    add_library(rb_net_tnfs STATIC ${rb_srcs})
    target_include_directories(rb_net_tnfs PUBLIC include)
    target_link_libraries(rb_net_tnfs PUBLIC rb_core rb_netsio)
    rb_apply_build_flags(rb_net_tnfs)
endif()
