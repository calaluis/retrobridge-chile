# rb_esp_storage — microSD initialisation and FAT mount in the ESP-IDF VFS (H3b).
# No file logic: rb_file_storage reads the images through the VFS (ADR-015).
# Every entry point validates the board profile first; with the default
# RB_BOARD_UNCONFIRMED nothing touches the card bus or any GPIO (H3-11).
# The driver glue is C (ADR-001): ESP-IDF's configuration macros use C99
# designated initialisers.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rb_build_flags.cmake)

idf_component_register(
    SRCS "src/sd_card_storage.cpp" "src/sd_card_driver.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES rb_core rb_board
    PRIV_REQUIRES fatfs sdmmc esp_driver_sdmmc esp_driver_sdspi esp_driver_spi)

rb_apply_build_flags(${COMPONENT_LIB})
