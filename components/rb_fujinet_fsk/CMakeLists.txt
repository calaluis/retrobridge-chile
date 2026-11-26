# rb_fujinet_fsk — the owner's FSK playback code from FujiNet, as it is (ADR-012 amendment 2), and the
# RetroBridge adapters it needs (cassette-motor-fujinet). Provenance: FUENTE.md.
#   lib/media/atari, lib/device/sio: the owner's files, beta v1.6.2-atari-fsk-beta.1 (commit cf3382f),
#     with their FujiNet layout so their own #include lines stay as they are. One change, recorded in
#     FUENTE.md: cassetteFSK.cpp loses two RMT flags that ESP-IDF 6.1 no longer has.
#   adapt, lib/include: RetroBridge headers with the FujiNet names that code uses (fnio.h, bus.h, ...).
#   include/rb/fujinet, src: RetroBridge adapters (FujiTapeLoop, EspFskEngine).
# The ESP-IDF sources build in every firmware (so CI compiles them); only the variant
# RB_CAS_ENGINE_FUJINET links them (main/Kconfig.projbuild).
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rb_build_flags.cmake)

set(rb_fujinet_pure_srcs
    "lib/media/atari/casFSK.cpp"
    "lib/media/atari/casFSKLoader.cpp"
    "lib/device/sio/cassetteFSKRead.cpp"
    "src/fujinet_adapt.cpp"
    "src/fuji_tape_loop.cpp"
    "src/fuji_tape_runner.cpp")
set(rb_fujinet_esp_srcs
    "lib/device/sio/cassetteFSK.cpp"
    "lib/device/sio/cassetteFSKLoader.cpp"
    "src/esp_fsk_engine.cpp")
set(rb_fujinet_private_dirs "adapt" "lib/device/sio" "lib/media/atari")

if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${rb_fujinet_pure_srcs} ${rb_fujinet_esp_srcs}
        INCLUDE_DIRS "include"
        PRIV_INCLUDE_DIRS ${rb_fujinet_private_dirs}
        REQUIRES rb_core rb_cassette
        PRIV_REQUIRES esp_driver_rmt esp_driver_gpio esp_hal_rmt esp_hal_uart esp_rom soc heap log freertos
                      pthread)
    rb_apply_build_flags(${COMPONENT_LIB})
    # The owner's code is built the way the FujiNet Atari build builds it.
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BUILD_ATARI)
    # ESP-IDF 6.1 names the owner's cassetteFSK.cpp needs (adapt/rb_fujinet_idf6.h).
    set_source_files_properties("lib/device/sio/cassetteFSK.cpp" PROPERTIES
        COMPILE_OPTIONS "-include;${CMAKE_CURRENT_LIST_DIR}/adapt/rb_fujinet_idf6.h")
else()
    # Host: the pure part. The test target adds the ESP-IDF sources over fake_idf (test/host/CMakeLists.txt).
    add_library(rb_fujinet_fsk STATIC ${rb_fujinet_pure_srcs})
    target_include_directories(rb_fujinet_fsk PUBLIC include ${rb_fujinet_private_dirs})
    target_link_libraries(rb_fujinet_fsk PUBLIC rb_core rb_cassette)
    rb_apply_build_flags(rb_fujinet_fsk)
endif()
