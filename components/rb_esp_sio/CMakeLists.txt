# rb_esp_sio — the physical SIO line on the ESP32 (menu-sio-fisico T4): UART2 and the COMMAND
# input behind rb_sio's SioUartPort. Only the menu-sio test package links it (H4 not approved).
# ESP-IDF only; the host tests build its source against test/host/fakes/idf.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rb_build_flags.cmake)

idf_component_register(
    SRCS "src/esp_sio_uart.cpp"
    INCLUDE_DIRS "include"
    REQUIRES rb_core rb_sio freertos
    PRIV_REQUIRES esp_driver_gpio esp_driver_uart esp_timer esp_rom log)

rb_apply_build_flags(${COMPONENT_LIB})
