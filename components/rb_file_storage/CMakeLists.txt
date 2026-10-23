# rb_file_storage — portable file adapter for disk images (ADR-015).
# Infrastructure: implements ImageRepository / ImageByteSource with the C stdio
# API. On the ESP32 that goes through the ESP-IDF VFS to FATFS on the microSD;
# on the host it uses real files, so the same code runs the contract suites.
# No ESP-IDF headers: only the C/C++ standard library and POSIX stat().
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rb_build_flags.cmake)

set(rb_srcs
    "src/image_path.cpp"
    "src/stdio_byte_source.cpp")

if(ESP_PLATFORM)
    idf_component_register(SRCS ${rb_srcs} INCLUDE_DIRS "include" REQUIRES rb_application)
    rb_apply_build_flags(${COMPONENT_LIB})
else()
    add_library(rb_file_storage STATIC ${rb_srcs})
    target_include_directories(rb_file_storage PUBLIC include)
    target_link_libraries(rb_file_storage PUBLIC rb_application)
    rb_apply_build_flags(rb_file_storage)
endif()
