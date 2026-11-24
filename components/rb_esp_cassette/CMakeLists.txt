# rb_esp_cassette — the cassette on the ESP32 (cassette-cas C106): MOTOR on its input-only
# GPIO behind the M3a gate, the tape files on the microSD, and the tasks that run rb_cassette.
# DATA IN (C107): UART and RMT, only with H4 and M3a signed. ESP-IDF only.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rb_build_flags.cmake)

idf_component_register(
    SRCS "src/cassette_service.cpp" "src/esp_data_in.cpp" "src/gpio_route.cpp"
    INCLUDE_DIRS "include"
    REQUIRES rb_core rb_board rb_cassette freertos
    PRIV_REQUIRES esp_driver_gpio esp_driver_uart esp_driver_rmt esp_hal_uart soc esp_timer heap log)

rb_apply_build_flags(${COMPONENT_LIB})
