# rb_domain — domain layer: disk drive context.
# Pure C++17: must not depend on ESP-IDF, FreeRTOS or hardware libraries (ADR-002).
# The same file builds as an ESP-IDF component and as a plain static library
# for the host tests.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rb_build_flags.cmake)

set(rb_srcs "src/build_checks.cpp")

if(ESP_PLATFORM)
    idf_component_register(SRCS ${rb_srcs} REQUIRES rb_core)
    rb_apply_build_flags(${COMPONENT_LIB})
else()
    add_library(rb_domain STATIC ${rb_srcs})
    target_link_libraries(rb_domain PUBLIC rb_core)
    rb_apply_build_flags(rb_domain)
endif()
