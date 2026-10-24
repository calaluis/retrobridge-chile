# rb_hw_diag — autonomous firmware diagnostics (H4): reset history, heap,
# watchdog configuration, storage health, latencies, a bounded event log and a
# CRC-framed report for UART0 captures.
# Pure C++17 (only rb_core): the platform side is the SystemProbe port,
# implemented by rb_esp_platform. Knows nothing about the Atari domain or ESP-IDF.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rb_build_flags.cmake)

set(rb_srcs "src/diag_types.cpp" "src/diagnostics_recorder.cpp" "src/report_writer.cpp")

if(ESP_PLATFORM)
    idf_component_register(SRCS ${rb_srcs} INCLUDE_DIRS "include" REQUIRES rb_core)
    rb_apply_build_flags(${COMPONENT_LIB})
else()
    add_library(rb_hw_diag STATIC ${rb_srcs})
    target_include_directories(rb_hw_diag PUBLIC include)
    target_link_libraries(rb_hw_diag PUBLIC rb_core)
    rb_apply_build_flags(rb_hw_diag)
endif()
