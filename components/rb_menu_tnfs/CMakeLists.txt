# rb_menu_tnfs — the TNFS loader of the menu over UDP (menu-sio-wifi-tnfs T3): TnfsLoader (one thread
# that lists TNFS folders and brings whole .atr/.xex images into the MemoryImageStore), the TNFS link
# of a source, the UDP port and the system name resolver. Shared by retrobridge_host (PC) and the
# menu-sio firmware (FujiNet, lwIP sockets), so E2 reuses the TNFS/XEX flow that works on the PC.
# Platform adapter, not a pure layer: it uses BSD sockets and std::thread (pthread on ESP-IDF).
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rb_build_flags.cmake)

set(rb_srcs
    "src/tnfs_loader.cpp"
    "src/tnfs_link.cpp"
    "src/udp_tnfs_port.cpp"
    "src/udp_datagram_port.cpp")

if(ESP_PLATFORM)
    idf_component_register(SRCS ${rb_srcs} INCLUDE_DIRS "include"
                           REQUIRES rb_core rb_application rb_netsio rb_net_tnfs rb_menu lwip pthread)
    rb_apply_build_flags(${COMPONENT_LIB})
else()
    add_library(rb_menu_tnfs STATIC ${rb_srcs})
    target_include_directories(rb_menu_tnfs PUBLIC include)
    target_link_libraries(rb_menu_tnfs PUBLIC rb_menu rb_net_tnfs rb_netsio)
    rb_apply_build_flags(rb_menu_tnfs)
    find_package(Threads REQUIRED)
    target_link_libraries(rb_menu_tnfs PUBLIC Threads::Threads)
    if(WIN32)
        target_link_libraries(rb_menu_tnfs PUBLIC ws2_32)
    endif()
endif()
