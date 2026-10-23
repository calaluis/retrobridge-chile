# rb_esp_platform — ESP32 implementations of platform ports (H3b): EspClock
# (Clock, esp_timer) and memory diagnostics (heap_caps). ESP-IDF only.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rb_build_flags.cmake)

idf_component_register(
    SRCS "src/esp_clock.cpp" "src/memory_snapshot.cpp"
    INCLUDE_DIRS "include"
    REQUIRES rb_application esp_timer heap freertos esp_rom)

rb_apply_build_flags(${COMPONENT_LIB})
