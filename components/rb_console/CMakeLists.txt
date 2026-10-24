# rb_console — diagnostic console over the ESP-IDF stdio console (UART0 through
# the board's USB-serial bridge on the ESP32) (H3b). Thin commands over the
# storage-context diagnostics; no access to the SIO bus. ESP-IDF only.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rb_build_flags.cmake)

idf_component_register(
    SRCS "src/diagnostic_console.cpp"
    INCLUDE_DIRS "include"
    REQUIRES rb_application rb_board rb_file_storage rb_esp_storage rb_esp_platform rb_hw_diag
    PRIV_REQUIRES console esp_rom vfs)

rb_apply_build_flags(${COMPONENT_LIB})
