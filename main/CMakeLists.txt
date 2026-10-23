# main — composition root. The only place that wires pure components to
# infrastructure adapters (ADR-002). Infrastructure components join in H3+.
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/rb_build_flags.cmake)

idf_component_register(
    SRCS "app_main.cpp"
    REQUIRES rb_core rb_domain rb_application rb_sio log)

rb_apply_build_flags(${COMPONENT_LIB})
