# main — composition root. The only place that wires pure components to
# infrastructure adapters (ADR-002). H3b: diagnostic firmware, no SIO.
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/rb_build_flags.cmake)

set(rb_main_srcs "app_main.cpp")
if(CONFIG_RB_BENCH_C8)
    list(APPEND rb_main_srcs "bench_c8.cpp")  # bench test C8 only (H4-C)
endif()
# REQUIRES cannot depend on sdkconfig: ESP-IDF expands component requirements before it reads the
# configuration, so CONFIG_* is empty there. The menu-sio components are always required (and compiled);
# only menu_sio.cpp is conditional, and nothing else references them, so other builds do not link them.
set(rb_main_requires rb_core rb_domain rb_application rb_sio rb_file_storage rb_board rb_esp_platform rb_esp_storage
    rb_console rb_hw_diag rb_esp_cassette rb_cassette log freertos soc
    rb_menu rb_esp_sio console esp_adc esp_driver_gpio esp_timer heap esp_app_format
    rb_menu_tnfs esp_wifi esp_netif esp_event lwip pthread)  # E2: Wi-Fi and TNFS (menu-sio-wifi-tnfs)
if(CONFIG_RB_MENU_SIO)
    list(APPEND rb_main_srcs "menu_sio.cpp" "wifi_station.cpp" "esp_tape_deck.cpp"  # test package menu-sio only (menu-sio-fisico, E2; menu-cinta T5)
         "cassette_ports.cpp")  # menu-cinta T9a.7: the atomics of the cassette ports, checked alone in CI
endif()

idf_component_register(
    SRCS ${rb_main_srcs}
    REQUIRES ${rb_main_requires})

rb_apply_build_flags(${COMPONENT_LIB})
