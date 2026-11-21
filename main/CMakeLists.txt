# main — composition root. The only place that wires pure components to
# infrastructure adapters (ADR-002). H3b: diagnostic firmware, no SIO.
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/rb_build_flags.cmake)

idf_component_register(
    SRCS "app_main.cpp"
    REQUIRES rb_core rb_domain rb_application rb_sio rb_file_storage rb_board rb_esp_platform rb_esp_storage
             rb_console rb_hw_diag rb_esp_cassette log)

rb_apply_build_flags(${COMPONENT_LIB})
