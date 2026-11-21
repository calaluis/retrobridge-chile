# rb_esp_cassette — the cassette on the ESP32 (cassette-cas C106): MOTOR on its input-only
# GPIO behind the M3a gate, the tape files on the microSD, and the tasks that run rb_cassette.
# No SIO output before H4: DATA IN is not driven. ESP-IDF only.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rb_build_flags.cmake)

idf_component_register(
    SRCS "src/cassette_service.cpp"
    INCLUDE_DIRS "include"
    REQUIRES rb_core rb_board rb_cassette freertos
    PRIV_REQUIRES esp_driver_gpio esp_timer heap log)

rb_apply_build_flags(${COMPONENT_LIB})
