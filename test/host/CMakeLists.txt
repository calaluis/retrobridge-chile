# Host test build (ADR-010): CMake + CTest + Unity, without ESP-IDF.
# Builds the pure components with the same flags as the firmware.
#
#   cmake -S test/host -B build/host -G Ninja [-DRB_SANITIZE=ON]
#   cmake --build build/host
#   ctest --test-dir build/host --output-on-failure
cmake_minimum_required(VERSION 3.22)
project(retrobridge_host_tests LANGUAGES C CXX)

option(RB_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(RB_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

set(rb_root ${CMAKE_CURRENT_LIST_DIR}/../..)
include(${rb_root}/cmake/rb_build_flags.cmake)

add_subdirectory(${rb_root}/components/rb_core rb_core)
add_subdirectory(${rb_root}/components/rb_domain rb_domain)
add_subdirectory(${rb_root}/components/rb_application rb_application)
add_subdirectory(${rb_root}/components/rb_sio rb_sio)
add_subdirectory(${rb_root}/components/rb_file_storage rb_file_storage)
add_subdirectory(${rb_root}/components/rb_board rb_board)
add_subdirectory(${rb_root}/components/rb_hw_diag rb_hw_diag)
add_subdirectory(${rb_root}/components/rb_netsio rb_netsio)
add_subdirectory(${rb_root}/components/rb_net_tnfs rb_net_tnfs)
add_subdirectory(${rb_root}/components/rb_cassette rb_cassette)
add_subdirectory(${rb_root}/components/rb_menu rb_menu)

# Unity v2.7.0, vendored (D-9). Third-party code: built with its own defaults.
add_library(unity STATIC unity/src/unity.c)
target_include_directories(unity PUBLIC unity/src)

enable_testing()

function(rb_add_host_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE unity rb_sio rb_file_storage)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
    rb_apply_build_flags(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Death test: runs `target arg` and passes only if it aborts (RB_ASSERT).
function(rb_add_abort_test name target arg)
    add_test(NAME ${name}
             COMMAND ${CMAKE_COMMAND} -DTEST_BINARY=$<TARGET_FILE:${target}> -DTEST_ARG=${arg}
                     -P ${CMAKE_CURRENT_LIST_DIR}/expect_abort.cmake)
endfunction()

rb_add_host_test(test_toolchain toolchain/test_toolchain.cpp)

# rb_core
rb_add_host_test(test_result core/test_result.cpp)
rb_add_host_test(test_span core/test_span.cpp)
# A Span invariant violation must abort through RB_ASSERT.
rb_add_abort_test(test_span_rejects_null_with_size test_span --construct-null-with-size)

# rb_domain
rb_add_host_test(test_value_objects domain/test_value_objects.cpp)
rb_add_host_test(test_atr_layout domain/test_atr_layout.cpp)
rb_add_host_test(test_disk_image domain/test_disk_image.cpp)
rb_add_host_test(test_virtual_drive domain/test_virtual_drive.cpp)
rb_add_host_test(test_disk_write domain/test_disk_write.cpp)

# rb_application
rb_add_host_test(test_image_location application/test_image_location.cpp)
rb_add_host_test(test_port_contracts application/test_port_contracts.cpp)
rb_add_host_test(test_drive_mailbox application/test_drive_mailbox.cpp)
rb_add_host_test(test_drive_bay application/test_drive_bay.cpp)
rb_add_host_test(test_drive_lifecycle application/test_drive_lifecycle.cpp)
rb_add_abort_test(test_drive_bay_invalid_count_aborts test_drive_bay --invalid-drive-count)

# rb_sio
rb_add_host_test(test_sio_checksum sio/test_sio_checksum.cpp)
rb_add_host_test(test_command_frame sio/test_command_frame.cpp)
rb_add_host_test(test_status_frame_encoder sio/test_status_frame_encoder.cpp)
rb_add_host_test(test_sio_dispatcher sio/test_sio_dispatcher.cpp)
rb_add_abort_test(test_dispatcher_execute_without_accept_aborts test_sio_dispatcher --execute-without-accept)
rb_add_host_test(test_sio_session sio/test_sio_session.cpp)
# SioLineTransport over a scripted line (menu-sio-fisico T3).
rb_add_host_test(test_sio_line_transport sio/test_sio_line_transport.cpp)
rb_add_host_test(test_sio_session_allocations sio/test_sio_session_allocations.cpp)
rb_add_host_test(test_command_line_tracker sio/test_command_line_tracker.cpp)
rb_add_host_test(test_sio_listener sio/test_sio_listener.cpp)
rb_add_abort_test(test_sio_listener_read_past_size_aborts test_sio_listener --read-past-size)
rb_add_host_test(test_sio_listener_allocations sio/test_sio_listener_allocations.cpp)
rb_add_host_test(test_command_frame_assembler sio/test_command_frame_assembler.cpp)
rb_add_host_test(test_command_frame_assembler_allocations sio/test_command_frame_assembler_allocations.cpp)
rb_add_host_test(test_sio_listen_chain sio/test_sio_listen_chain.cpp)

# rb_netsio (Issue #32 E1a, ADR-016)
rb_add_host_test(test_netsio_codec netsio/test_netsio_codec.cpp)
target_link_libraries(test_netsio_codec PRIVATE rb_netsio)
rb_add_host_test(test_netsio_transport netsio/test_netsio_transport.cpp)
target_link_libraries(test_netsio_transport PRIVATE rb_netsio)
rb_add_host_test(test_netsio_transport_allocations netsio/test_netsio_transport_allocations.cpp)
target_link_libraries(test_netsio_transport_allocations PRIVATE rb_netsio)

# rb_net_tnfs (menu-tnfs, ADR-012): read-only TNFS codec
rb_add_host_test(test_tnfs_codec net_tnfs/test_tnfs_codec.cpp)
target_link_libraries(test_tnfs_codec PRIVATE rb_net_tnfs)
rb_add_host_test(test_fake_tnfs_server net_tnfs/test_fake_tnfs_server.cpp)
target_link_libraries(test_fake_tnfs_server PRIVATE rb_netsio)
rb_add_host_test(test_tnfs_client net_tnfs/test_tnfs_client.cpp)
target_link_libraries(test_tnfs_client PRIVATE rb_net_tnfs)
# UdpTnfsPort over a real loopback socket (menu-tnfs T008), FakeTnfsServer on a thread.
find_package(Threads REQUIRED)
rb_add_host_test(test_udp_tnfs_port net_tnfs/test_udp_tnfs_port.cpp
                 ${rb_root}/host/src/udp_tnfs_port.cpp ${rb_root}/host/src/udp_datagram_port.cpp)
target_include_directories(test_udp_tnfs_port PRIVATE ${rb_root}/host/src ${rb_root}/components/rb_menu/include)
target_link_libraries(test_udp_tnfs_port PRIVATE rb_net_tnfs Threads::Threads)
if(WIN32)
    target_link_libraries(test_udp_tnfs_port PRIVATE ws2_32)
endif()

# rb_file_storage (H3a)
rb_add_host_test(test_image_path storage/test_image_path.cpp)

set(rb_storage_data ${CMAKE_CURRENT_BINARY_DIR}/storage-data)
file(MAKE_DIRECTORY ${rb_storage_data}/sub)
rb_add_host_test(test_stdio_storage storage/test_stdio_storage.cpp)
target_compile_definitions(test_stdio_storage PRIVATE RB_TEST_DATA_DIR="${rb_storage_data}")

# Meta A on the host: images and manifest generated at test time by the
# independent Python oracle (nothing is stored in the repository).
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(rb_test_images ${CMAKE_CURRENT_BINARY_DIR}/test-images)
add_test(NAME generate_atr_images
         COMMAND ${Python3_EXECUTABLE} -I ${rb_root}/tools/make_atr.py --out ${rb_test_images})
set_tests_properties(generate_atr_images PROPERTIES FIXTURES_SETUP atr_images)
rb_add_host_test(test_meta_a storage/test_meta_a.cpp)
target_compile_definitions(test_meta_a PRIVATE RB_TEST_IMAGES_DIR="${rb_test_images}")
set_tests_properties(test_meta_a PROPERTIES FIXTURES_REQUIRED atr_images)

# Diagnostics (H3b)
rb_add_host_test(test_latency_stats diagnostics/test_latency_stats.cpp)
rb_add_host_test(test_diagnostics diagnostics/test_diagnostics.cpp)

# rb_board (H3b)
rb_add_host_test(test_board_profile board/test_board_profile.cpp)
target_link_libraries(test_board_profile PRIVATE rb_board)
rb_add_host_test(test_fujinet_profile board/test_fujinet_profile.cpp)
target_link_libraries(test_fujinet_profile PRIVATE rb_board)
# MOTOR (SIO pin 8) on GPIO36, input only, behind the M3a gate (cassette-cas C106).
rb_add_host_test(test_motor_assignment board/test_motor_assignment.cpp)
target_link_libraries(test_motor_assignment PRIVATE rb_board)
rb_add_host_test(test_cassette_output_assignment board/test_cassette_output_assignment.cpp)
target_link_libraries(test_cassette_output_assignment PRIVATE rb_board)
rb_add_host_test(test_sio_pins board/test_sio_pins.cpp)
target_link_libraries(test_sio_pins PRIVATE rb_board)
rb_add_host_test(test_sio_listen_assignment board/test_sio_listen_assignment.cpp)
target_link_libraries(test_sio_listen_assignment PRIVATE rb_board)
# rb_hw_diag (H4, autonomous diagnostics)
rb_add_host_test(test_event_log hwdiag/test_event_log.cpp)
target_link_libraries(test_event_log PRIVATE rb_hw_diag)
rb_add_host_test(test_diagnostics_recorder hwdiag/test_diagnostics_recorder.cpp)
target_link_libraries(test_diagnostics_recorder PRIVATE rb_hw_diag)
set(rb_diag_sample ${CMAKE_CURRENT_BINARY_DIR}/diag-report-sample.txt)
rb_add_host_test(test_report_writer hwdiag/test_report_writer.cpp)
target_link_libraries(test_report_writer PRIVATE rb_hw_diag)
target_compile_definitions(test_report_writer PRIVATE RB_DIAG_SAMPLE_FILE="${rb_diag_sample}")
set_tests_properties(test_report_writer PROPERTIES FIXTURES_SETUP diag_sample)
# The firmware's report format, checked by the independent PC tool.
add_test(NAME check_diag_report_sample
         COMMAND ${Python3_EXECUTABLE} -I ${rb_root}/tools/check_diag_report.py ${rb_diag_sample})
set_tests_properties(check_diag_report_sample PROPERTIES FIXTURES_REQUIRED diag_sample)

rb_add_abort_test(test_repository_close_attached_aborts test_port_contracts --close-attached-source)
rb_add_abort_test(test_mailbox_reply_without_request_aborts test_drive_mailbox --reply-without-request)

# retrobridge_host boot menu (Issue #38, ADR-017): the host's menu sources are
# plain C++ over the components, so CI tests them here without building host/.
set(rb_menu_images ${CMAKE_CURRENT_BINARY_DIR}/menu-images)
add_test(NAME generate_menu_images
         COMMAND ${Python3_EXECUTABLE} -I ${rb_root}/tools/make_atr.py --out ${rb_menu_images} --demo)
set_tests_properties(generate_menu_images PROPERTIES FIXTURES_SETUP menu_images)
rb_add_host_test(test_host_menu menu/test_host_menu.cpp
                 ${rb_root}/components/rb_menu/src/create_atr.cpp ${rb_root}/components/rb_menu/src/menu_catalog.cpp ${rb_root}/components/rb_menu/src/menu_image.cpp
                 ${rb_root}/components/rb_menu/src/menu_controller.cpp ${rb_root}/components/rb_menu/src/drive_memory.cpp ${rb_root}/components/rb_menu/src/menu_sio_transport.cpp
                 ${rb_root}/components/rb_menu/src/menu_sources.cpp ${rb_root}/components/rb_menu/src/tnfs_folder.cpp)
target_include_directories(test_host_menu PRIVATE ${rb_root}/host/src ${rb_root}/components/rb_menu/include)
target_compile_definitions(test_host_menu PRIVATE RB_TEST_MENU_DIR="${rb_menu_images}")

# MenuStation (menu-sio-fisico T2): the menu as D1 over any SioTransport, from rb_menu.
rb_add_host_test(test_menu_station menu/test_menu_station.cpp)
target_link_libraries(test_menu_station PRIVATE rb_menu)
target_compile_definitions(test_menu_station PRIVATE RB_TEST_MENU_DIR="${rb_menu_images}")
set_tests_properties(test_menu_station PROPERTIES FIXTURES_REQUIRED menu_images)
set_tests_properties(test_host_menu PROPERTIES FIXTURES_REQUIRED menu_images)
# Persistent drives in the controller (menu-unidades-persistentes T3).
rb_add_host_test(test_menu_unidades menu/test_menu_unidades.cpp
                 ${rb_root}/components/rb_menu/src/create_atr.cpp ${rb_root}/components/rb_menu/src/menu_catalog.cpp ${rb_root}/components/rb_menu/src/menu_image.cpp
                 ${rb_root}/components/rb_menu/src/menu_controller.cpp ${rb_root}/components/rb_menu/src/menu_sources.cpp
                 ${rb_root}/components/rb_menu/src/tnfs_folder.cpp ${rb_root}/components/rb_menu/src/drive_memory.cpp)
target_include_directories(test_menu_unidades PRIVATE ${rb_root}/host/src ${rb_root}/components/rb_menu/include)
target_compile_definitions(test_menu_unidades PRIVATE RB_TEST_MENU_DIR="${rb_menu_images}")
set_tests_properties(test_menu_unidades PROPERTIES FIXTURES_REQUIRED menu_images)
# Disk writes with --images (menu-escritura-disco, W005).
rb_add_host_test(test_host_write menu/test_host_write.cpp
                 ${rb_root}/components/rb_menu/src/create_atr.cpp ${rb_root}/components/rb_menu/src/menu_catalog.cpp ${rb_root}/components/rb_menu/src/menu_image.cpp
                 ${rb_root}/components/rb_menu/src/menu_controller.cpp ${rb_root}/components/rb_menu/src/drive_memory.cpp ${rb_root}/components/rb_menu/src/menu_sio_transport.cpp
                 ${rb_root}/components/rb_menu/src/menu_sources.cpp ${rb_root}/components/rb_menu/src/tnfs_folder.cpp)
target_include_directories(test_host_write PRIVATE ${rb_root}/host/src ${rb_root}/components/rb_menu/include)
target_compile_definitions(test_host_write PRIVATE RB_TEST_MENU_DIR="${rb_menu_images}"
                           RB_TEST_WRITE_DIR="${CMAKE_CURRENT_BINARY_DIR}/write-images")
target_link_libraries(test_host_write PRIVATE Threads::Threads)
set_tests_properties(test_host_write PROPERTIES FIXTURES_REQUIRED menu_images)
# E on the main screen: edit or add a server and rewrite --sources (menu-editar-servidor, M43).
rb_add_host_test(test_menu_editar menu/test_menu_editar.cpp
                 ${rb_root}/components/rb_menu/src/create_atr.cpp ${rb_root}/components/rb_menu/src/menu_catalog.cpp ${rb_root}/components/rb_menu/src/menu_image.cpp
                 ${rb_root}/components/rb_menu/src/menu_controller.cpp ${rb_root}/components/rb_menu/src/drive_memory.cpp ${rb_root}/components/rb_menu/src/menu_sio_transport.cpp
                 ${rb_root}/components/rb_menu/src/menu_sources.cpp ${rb_root}/components/rb_menu/src/tnfs_folder.cpp)
target_include_directories(test_menu_editar PRIVATE ${rb_root}/host/src ${rb_root}/components/rb_menu/include)
target_compile_definitions(test_menu_editar PRIVATE RB_TEST_MENU_DIR="${rb_menu_images}")
set_tests_properties(test_menu_editar PROPERTIES FIXTURES_REQUIRED menu_images)
# Memory slots for images brought over TNFS and the *tnfs*<n> locations (menu-tnfs T009).
find_package(Threads REQUIRED)
rb_add_host_test(test_memory_image_store menu/test_memory_image_store.cpp ${rb_root}/components/rb_menu/src/menu_image.cpp)
target_include_directories(test_memory_image_store PRIVATE ${rb_root}/host/src ${rb_root}/components/rb_menu/include)
target_compile_definitions(test_memory_image_store PRIVATE RB_TEST_MENU_DIR="${rb_menu_images}")
target_link_libraries(test_memory_image_store PRIVATE Threads::Threads)
set_tests_properties(test_memory_image_store PROPERTIES FIXTURES_REQUIRED menu_images)

# The listing of a TNFS folder: filter, order, 20 entries and display names (menu-tnfs T010).
# The local list: up to 999 .atr names (menu-navegacion N002).
rb_add_host_test(test_menu_catalog menu/test_menu_catalog.cpp ${rb_root}/components/rb_menu/src/menu_catalog.cpp)
target_include_directories(test_menu_catalog PRIVATE ${rb_root}/host/src ${rb_root}/components/rb_menu/include)
rb_add_host_test(test_drive_memory menu/test_drive_memory.cpp ${rb_root}/components/rb_menu/src/drive_memory.cpp)
target_include_directories(test_drive_memory PRIVATE ${rb_root}/components/rb_menu/include)
rb_add_host_test(test_sio_power_watch menu/test_sio_power_watch.cpp)
target_include_directories(test_sio_power_watch PRIVATE ${rb_root}/components/rb_menu/include)
target_link_libraries(test_sio_power_watch PRIVATE rb_menu)
rb_add_host_test(test_drive_slot_store menu/test_drive_slot_store.cpp ${rb_root}/components/rb_menu/src/drive_slot_store.cpp
                 ${rb_root}/components/rb_menu/src/drive_memory.cpp)
target_include_directories(test_drive_slot_store PRIVATE ${rb_root}/components/rb_menu/include)
target_compile_definitions(test_drive_slot_store PRIVATE RB_TEST_SLOT_DIR="${CMAKE_CURRENT_BINARY_DIR}/drive-slots")
# The firmware's drive-memory glue without FreeRTOS (menu-unidades-persistentes T6).
rb_add_host_test(test_drive_memory_link menu/test_drive_memory_link.cpp
                 ${rb_root}/components/rb_menu/src/create_atr.cpp ${rb_root}/components/rb_menu/src/menu_catalog.cpp ${rb_root}/components/rb_menu/src/menu_image.cpp
                 ${rb_root}/components/rb_menu/src/menu_controller.cpp ${rb_root}/components/rb_menu/src/menu_sources.cpp
                 ${rb_root}/components/rb_menu/src/tnfs_folder.cpp ${rb_root}/components/rb_menu/src/drive_memory.cpp
                 ${rb_root}/components/rb_menu/src/drive_slot_store.cpp ${rb_root}/components/rb_menu/src/drive_memory_link.cpp)
target_include_directories(test_drive_memory_link PRIVATE ${rb_root}/host/src ${rb_root}/components/rb_menu/include)
target_compile_definitions(test_drive_memory_link PRIVATE RB_TEST_MENU_DIR="${rb_menu_images}"
                           RB_TEST_SLOT_DIR="${CMAKE_CURRENT_BINARY_DIR}/drive-link-slots")
set_tests_properties(test_drive_memory_link PROPERTIES FIXTURES_REQUIRED menu_images)
# Creating a blank ATR from the menu (menu-formatear-disco, F003).
rb_add_host_test(test_create_atr menu/test_create_atr.cpp ${rb_root}/components/rb_menu/src/create_atr.cpp
                 ${rb_root}/components/rb_menu/src/menu_catalog.cpp)
target_include_directories(test_create_atr PRIVATE ${rb_root}/host/src ${rb_root}/components/rb_menu/include)
target_compile_definitions(test_create_atr PRIVATE RB_TEST_CREATE_DIR="${CMAKE_CURRENT_BINARY_DIR}/create-images")

# Checking a .xex and wrapping it with the 6502 loader (menu-xex X002).
rb_add_host_test(test_xex_image menu/test_xex_image.cpp ${rb_root}/components/rb_menu/src/xex_image.cpp)
target_include_directories(test_xex_image PRIVATE ${rb_root}/host/src ${rb_root}/components/rb_menu/include)

# rb_cassette (cassette-cas C101): records and FSK runs of a .cas. Real tapes come from
# RB_CAS_SAMPLES (outside the repo); without it those two cases are ignored.
rb_add_host_test(test_cas_index cassette/test_cas_index.cpp)
target_link_libraries(test_cas_index PRIVATE rb_cassette)
# The FSK signal of a run: levels and portions (cassette-cas C102).
rb_add_host_test(test_fsk_wave cassette/test_fsk_wave.cpp)
target_link_libraries(test_fsk_wave PRIVATE rb_cassette)
# Loading an FSK run a block at a time, start gate, retries, no allocation (cassette-cas C103).
rb_add_host_test(test_fsk_loader cassette/test_fsk_loader.cpp)
target_link_libraries(test_fsk_loader PRIVATE rb_cassette)
# The player: records with MOTOR, FSK runs through the loader, MOTOR drop in a run (cassette-cas C104).
rb_add_host_test(test_rb_cassette_player cassette/test_cassette_player.cpp)
target_link_libraries(test_rb_cassette_player PRIVATE rb_cassette)
# `cas mount|status|eject|rewind`: a bad file keeps the tape, closing waits for the producer (cassette-cas C105).
rb_add_host_test(test_cassette_commands cassette/test_cassette_commands.cpp)
target_link_libraries(test_cassette_commands PRIVATE rb_cassette)
# Tapes read through stdio and outputs that drive no pin before H4 (cassette-cas C106).
rb_add_host_test(test_stdio_tape_files cassette/test_stdio_tape_files.cpp)
find_package(Threads REQUIRED)
target_link_libraries(test_stdio_tape_files PRIVATE rb_cassette Threads::Threads)
rb_add_host_test(test_fsk_symbol_feed cassette/test_fsk_symbol_feed.cpp)
target_link_libraries(test_fsk_symbol_feed PRIVATE rb_cassette Threads::Threads)

# rb_esp_cassette's DATA IN driver against a scripted model of the ESP-IDF calls (H4-C C8):
# who owns GPIO21 after each step, and the fault when the UART cannot take it back.
add_library(fake_idf STATIC fakes/idf/fake_idf.cpp)
target_include_directories(fake_idf PUBLIC fakes/idf ${CMAKE_CURRENT_LIST_DIR})
rb_apply_build_flags(fake_idf)
rb_add_host_test(test_esp_data_in esp_cassette/test_esp_data_in.cpp
                 ${rb_root}/components/rb_esp_cassette/src/esp_data_in.cpp)
target_include_directories(test_esp_data_in PRIVATE ${rb_root}/components/rb_esp_cassette/include)
target_link_libraries(test_esp_data_in PRIVATE fake_idf rb_cassette rb_board)

# EspSioUart against the fake ESP-IDF (menu-sio-fisico T4).
rb_add_host_test(test_esp_sio_uart esp_sio/test_esp_sio_uart.cpp ${rb_root}/components/rb_esp_sio/src/esp_sio_uart.cpp)
target_include_directories(test_esp_sio_uart PRIVATE ${rb_root}/components/rb_esp_sio/include)
target_link_libraries(test_esp_sio_uart PRIVATE fake_idf)


rb_add_host_test(test_tnfs_folder menu/test_tnfs_folder.cpp ${rb_root}/components/rb_menu/src/tnfs_folder.cpp
                 ${rb_root}/components/rb_menu/src/menu_catalog.cpp)
target_include_directories(test_tnfs_folder PRIVATE ${rb_root}/host/src ${rb_root}/components/rb_menu/include)

# SHA-256 of the images loaded over TNFS (menu-tnfs T004). RB_REF_ATR, when set,
# points at the spec's reference ATR (not in the repository).
rb_add_host_test(test_sha256 menu/test_sha256.cpp ${rb_root}/components/rb_menu/src/sha256.cpp)
target_include_directories(test_sha256 PRIVATE ${rb_root}/host/src ${rb_root}/components/rb_menu/include)

# The TNFS loading thread: jobs, cancel, late results and errors (menu-tnfs T011).
rb_add_host_test(test_tnfs_loader menu/test_tnfs_loader.cpp ${rb_root}/host/src/tnfs_loader.cpp ${rb_root}/components/rb_menu/src/xex_image.cpp
                 ${rb_root}/components/rb_menu/src/tnfs_folder.cpp ${rb_root}/components/rb_menu/src/menu_catalog.cpp
                 ${rb_root}/components/rb_menu/src/sha256.cpp)
target_include_directories(test_tnfs_loader PRIVATE ${rb_root}/host/src ${rb_root}/components/rb_menu/include)
target_link_libraries(test_tnfs_loader PRIVATE rb_net_tnfs Threads::Threads)

# TNFS sources in the menu: MenuController with the loader over the fake server (menu-tnfs T012).
rb_add_host_test(test_menu_tnfs menu/test_menu_tnfs.cpp ${rb_root}/components/rb_menu/src/menu_controller.cpp ${rb_root}/components/rb_menu/src/drive_memory.cpp
                 ${rb_root}/components/rb_menu/src/create_atr.cpp ${rb_root}/components/rb_menu/src/menu_catalog.cpp ${rb_root}/components/rb_menu/src/menu_image.cpp
                 ${rb_root}/components/rb_menu/src/menu_sources.cpp ${rb_root}/host/src/tnfs_loader.cpp ${rb_root}/components/rb_menu/src/xex_image.cpp
                 ${rb_root}/components/rb_menu/src/tnfs_folder.cpp ${rb_root}/components/rb_menu/src/sha256.cpp)
target_include_directories(test_menu_tnfs PRIVATE ${rb_root}/host/src ${rb_root}/components/rb_menu/include)
target_compile_definitions(test_menu_tnfs PRIVATE RB_TEST_MENU_DIR="${rb_menu_images}")
target_link_libraries(test_menu_tnfs PRIVATE rb_net_tnfs Threads::Threads)
set_tests_properties(test_menu_tnfs PROPERTIES FIXTURES_REQUIRED menu_images)
# Break back to the main screen (menu-unidades-persistentes T5, C7), on the rig of test_menu_tnfs.
rb_add_host_test(test_menu_break menu/test_menu_break.cpp ${rb_root}/components/rb_menu/src/menu_controller.cpp ${rb_root}/components/rb_menu/src/drive_memory.cpp
                 ${rb_root}/components/rb_menu/src/create_atr.cpp ${rb_root}/components/rb_menu/src/menu_catalog.cpp ${rb_root}/components/rb_menu/src/menu_image.cpp
                 ${rb_root}/components/rb_menu/src/menu_sources.cpp ${rb_root}/host/src/tnfs_loader.cpp ${rb_root}/components/rb_menu/src/xex_image.cpp
                 ${rb_root}/components/rb_menu/src/tnfs_folder.cpp ${rb_root}/components/rb_menu/src/sha256.cpp)
target_include_directories(test_menu_break PRIVATE ${rb_root}/host/src ${rb_root}/components/rb_menu/include)
target_compile_definitions(test_menu_break PRIVATE RB_TEST_MENU_DIR="${rb_menu_images}")
target_link_libraries(test_menu_break PRIVATE rb_net_tnfs Threads::Threads)
set_tests_properties(test_menu_break PROPERTIES FIXTURES_REQUIRED menu_images)

# Choosing and booting a .xex (menu-xex X004), on the rig of test_menu_tnfs with its own folder.
rb_add_host_test(test_menu_xex menu/test_menu_xex.cpp ${rb_root}/components/rb_menu/src/menu_controller.cpp ${rb_root}/components/rb_menu/src/drive_memory.cpp
                 ${rb_root}/components/rb_menu/src/create_atr.cpp ${rb_root}/components/rb_menu/src/menu_catalog.cpp ${rb_root}/components/rb_menu/src/menu_image.cpp
                 ${rb_root}/components/rb_menu/src/menu_sources.cpp ${rb_root}/host/src/tnfs_loader.cpp ${rb_root}/components/rb_menu/src/xex_image.cpp
                 ${rb_root}/components/rb_menu/src/tnfs_folder.cpp ${rb_root}/components/rb_menu/src/sha256.cpp)
target_include_directories(test_menu_xex PRIVATE ${rb_root}/host/src ${rb_root}/components/rb_menu/include)
target_compile_definitions(test_menu_xex PRIVATE RB_TEST_MENU_DIR="${CMAKE_CURRENT_BINARY_DIR}/xex-images")
target_link_libraries(test_menu_xex PRIVATE rb_net_tnfs Threads::Threads)
# menu-cinta T1: .cas files are listed and refused on D1-D8
rb_add_host_test(test_menu_cinta menu/test_menu_cinta.cpp ${rb_root}/components/rb_menu/src/menu_controller.cpp ${rb_root}/components/rb_menu/src/drive_memory.cpp
                 ${rb_root}/components/rb_menu/src/create_atr.cpp ${rb_root}/components/rb_menu/src/menu_catalog.cpp ${rb_root}/components/rb_menu/src/menu_image.cpp
                 ${rb_root}/components/rb_menu/src/menu_sources.cpp ${rb_root}/host/src/tnfs_loader.cpp ${rb_root}/components/rb_menu/src/xex_image.cpp
                 ${rb_root}/components/rb_menu/src/tnfs_folder.cpp ${rb_root}/components/rb_menu/src/sha256.cpp)
target_include_directories(test_menu_cinta PRIVATE ${rb_root}/host/src ${rb_root}/components/rb_menu/include)
target_compile_definitions(test_menu_cinta PRIVATE RB_TEST_MENU_DIR="${CMAKE_CURRENT_BINARY_DIR}/cinta-images")
target_link_libraries(test_menu_cinta PRIVATE rb_net_tnfs Threads::Threads)

# Cursor and pages in the lists (menu-navegacion N003), on the rig of test_menu_tnfs.
rb_add_host_test(test_menu_navegacion menu/test_menu_navegacion.cpp ${rb_root}/components/rb_menu/src/menu_controller.cpp ${rb_root}/components/rb_menu/src/drive_memory.cpp
                 ${rb_root}/components/rb_menu/src/create_atr.cpp ${rb_root}/components/rb_menu/src/menu_catalog.cpp ${rb_root}/components/rb_menu/src/menu_image.cpp
                 ${rb_root}/components/rb_menu/src/menu_sources.cpp ${rb_root}/host/src/tnfs_loader.cpp ${rb_root}/components/rb_menu/src/xex_image.cpp
                 ${rb_root}/components/rb_menu/src/tnfs_folder.cpp ${rb_root}/components/rb_menu/src/sha256.cpp)
target_include_directories(test_menu_navegacion PRIVATE ${rb_root}/host/src ${rb_root}/components/rb_menu/include)
target_compile_definitions(test_menu_navegacion PRIVATE RB_TEST_MENU_DIR="${rb_menu_images}")
target_link_libraries(test_menu_navegacion PRIVATE rb_net_tnfs Threads::Threads)
set_tests_properties(test_menu_navegacion PROPERTIES FIXTURES_REQUIRED menu_images)

# What main.cpp wires for TNFS: request trace, UDP link, shared JSONL log (menu-tnfs T013).
rb_add_host_test(test_tnfs_link menu/test_tnfs_link.cpp ${rb_root}/host/src/tnfs_link.cpp
                 ${rb_root}/host/src/udp_tnfs_port.cpp ${rb_root}/host/src/udp_datagram_port.cpp
                 ${rb_root}/host/src/jsonl_log.cpp ${rb_root}/host/src/tnfs_loader.cpp ${rb_root}/components/rb_menu/src/xex_image.cpp
                 ${rb_root}/components/rb_menu/src/tnfs_folder.cpp ${rb_root}/components/rb_menu/src/sha256.cpp
                 ${rb_root}/components/rb_menu/src/menu_controller.cpp ${rb_root}/components/rb_menu/src/drive_memory.cpp ${rb_root}/components/rb_menu/src/create_atr.cpp ${rb_root}/components/rb_menu/src/menu_catalog.cpp
                 ${rb_root}/components/rb_menu/src/menu_image.cpp ${rb_root}/components/rb_menu/src/menu_sources.cpp)
target_include_directories(test_tnfs_link PRIVATE ${rb_root}/host/src ${rb_root}/components/rb_menu/include)
target_compile_definitions(test_tnfs_link PRIVATE RB_TEST_MENU_DIR="${rb_menu_images}")
target_link_libraries(test_tnfs_link PRIVATE rb_net_tnfs rb_netsio Threads::Threads)
if(WIN32)
    target_link_libraries(test_tnfs_link PRIVATE ws2_32)
endif()
set_tests_properties(test_tnfs_link PROPERTIES FIXTURES_REQUIRED menu_images)
