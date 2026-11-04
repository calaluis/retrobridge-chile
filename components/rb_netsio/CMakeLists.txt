# rb_netsio — NetSIO transport (FEP-003): codec and SioTransport over a datagram port (ADR-016).
# Pure C++17: no sockets, ESP-IDF, FreeRTOS or hardware libraries (ADR-002). The
# datagram port is implemented outside this component (UDP on the PC host).
# The same file builds as an ESP-IDF component and as a plain static library
# for the host tests. The firmware does not require it (MINIMAL_BUILD, D3).
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rb_build_flags.cmake)

set(rb_srcs
    "src/netsio_codec.cpp"
    "src/netsio_sio_transport.cpp")

if(ESP_PLATFORM)
    idf_component_register(SRCS ${rb_srcs} INCLUDE_DIRS "include" REQUIRES rb_application)
    rb_apply_build_flags(${COMPONENT_LIB})
else()
    add_library(rb_netsio STATIC ${rb_srcs})
    target_include_directories(rb_netsio PUBLIC include)
    target_link_libraries(rb_netsio PUBLIC rb_application)
    rb_apply_build_flags(rb_netsio)
endif()
