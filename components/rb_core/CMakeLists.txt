# rb_core — shared kernel (Result, ErrorCode, Span, RB_ASSERT).
# Pure C++17: must not depend on ESP-IDF, FreeRTOS or hardware libraries (ADR-002).
# The same file builds as an ESP-IDF component and as a plain static library
# for the host tests.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rb_build_flags.cmake)

set(rb_srcs "src/build_checks.cpp")

if(ESP_PLATFORM)
    idf_component_register(SRCS ${rb_srcs} INCLUDE_DIRS "include")
    rb_apply_build_flags(${COMPONENT_LIB})
else()
    add_library(rb_core STATIC ${rb_srcs})
    target_include_directories(rb_core PUBLIC include)
    rb_apply_build_flags(rb_core)
endif()
