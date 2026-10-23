# rb_board — board profiles and GPIO validation (H3-11).
# Pure C++17 (ESP32 pin rules as data, no ESP-IDF headers), so the validator runs
# in the host tests. The active profile is chosen by Kconfig in main; the
# default, RB_BOARD_UNCONFIRMED, blocks every board-specific GPIO and bus.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rb_build_flags.cmake)

set(rb_srcs "src/board_profile.cpp")

if(ESP_PLATFORM)
    idf_component_register(SRCS ${rb_srcs} INCLUDE_DIRS "include" REQUIRES rb_core)
    rb_apply_build_flags(${COMPONENT_LIB})
else()
    add_library(rb_board STATIC ${rb_srcs})
    target_include_directories(rb_board PUBLIC include)
    target_link_libraries(rb_board PUBLIC rb_core)
    rb_apply_build_flags(rb_board)
endif()
