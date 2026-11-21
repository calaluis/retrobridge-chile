# rb_menu — the RetroBridge boot menu (ADR-017): controller, catalog, sources, menu disk,
# SIO decorator and their helpers. Shared by retrobridge_host and the firmware (menu-sio-fisico).
# Pure C++17 plus stdio/dirent, which both the PC and ESP-IDF provide: no ESP-IDF, FreeRTOS or
# sockets here. The same file builds as an ESP-IDF component and as a plain static library.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rb_build_flags.cmake)

set(rb_srcs
    "src/create_atr.cpp"
    "src/menu_catalog.cpp"
    "src/menu_controller.cpp"
    "src/menu_image.cpp"
    "src/menu_sio_transport.cpp"
    "src/menu_sources.cpp"
    "src/sha256.cpp"
    "src/tnfs_folder.cpp"
    "src/xex_image.cpp")

if(ESP_PLATFORM)
    idf_component_register(SRCS ${rb_srcs} INCLUDE_DIRS "include" REQUIRES rb_sio rb_file_storage)
    rb_apply_build_flags(${COMPONENT_LIB})
else()
    add_library(rb_menu STATIC ${rb_srcs})
    target_include_directories(rb_menu PUBLIC include)
    target_link_libraries(rb_menu PUBLIC rb_sio rb_file_storage)
    rb_apply_build_flags(rb_menu)
endif()
