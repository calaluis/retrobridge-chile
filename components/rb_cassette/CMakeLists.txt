# rb_cassette — the cassette (spec cassette-cas): reads A8CAS .cas tapes and decides
# what plays, adapted from the owner's own FSK code (ADR-012 amendment 1).
# Pure C++17: no ESP-IDF, FreeRTOS, RMT, UART or GPIO (ADR-002). The hardware lives
# behind ports, in a platform component. The same file builds as an ESP-IDF
# component and as a plain static library for the host tests.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rb_build_flags.cmake)

set(rb_srcs
    "src/cas_index.cpp"
    "src/fsk_wave.cpp"
    "src/fsk_loader.cpp")

if(ESP_PLATFORM)
    idf_component_register(SRCS ${rb_srcs} INCLUDE_DIRS "include" REQUIRES rb_core)
    rb_apply_build_flags(${COMPONENT_LIB})
else()
    add_library(rb_cassette STATIC ${rb_srcs})
    target_include_directories(rb_cassette PUBLIC include)
    target_link_libraries(rb_cassette PUBLIC rb_core)
    rb_apply_build_flags(rb_cassette)
endif()
