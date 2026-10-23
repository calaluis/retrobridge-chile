# rb_application — application layer: use cases and ports.
# Pure C++17: must not depend on ESP-IDF, FreeRTOS or hardware libraries (ADR-002).
# The same file builds as an ESP-IDF component and as a plain static library
# for the host tests.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rb_build_flags.cmake)

set(rb_srcs
    "src/build_checks.cpp"
    "src/image_location.cpp"
    "src/drive_bay.cpp"
    "src/mount_disk_image.cpp"
    "src/apply_drive_request.cpp")

if(ESP_PLATFORM)
    idf_component_register(SRCS ${rb_srcs} INCLUDE_DIRS "include" REQUIRES rb_domain)
    rb_apply_build_flags(${COMPONENT_LIB})
else()
    add_library(rb_application STATIC ${rb_srcs})
    target_include_directories(rb_application PUBLIC include)
    target_link_libraries(rb_application PUBLIC rb_domain)
    rb_apply_build_flags(rb_application)
endif()
