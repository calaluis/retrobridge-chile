# rb_esp_platform — ESP32 implementations of platform ports (H3b): EspClock
# (Clock, esp_timer) and memory diagnostics (heap_caps); EspSystemProbe
# (rb_hw_diag SystemProbe: reset reason, RTC reset history, watchdog settings,
# firmware identity; H4). ESP-IDF only.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rb_build_flags.cmake)

idf_component_register(
    SRCS "src/esp_clock.cpp" "src/memory_snapshot.cpp" "src/esp_system_probe.cpp"
    INCLUDE_DIRS "include"
    REQUIRES rb_application rb_hw_diag esp_timer heap freertos esp_rom esp_system esp_app_format)

rb_apply_build_flags(${COMPONENT_LIB})
