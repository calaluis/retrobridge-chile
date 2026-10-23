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

# Unity v2.7.0, vendored (D-9). Third-party code: built with its own defaults.
add_library(unity STATIC unity/src/unity.c)
target_include_directories(unity PUBLIC unity/src)

enable_testing()

function(rb_add_host_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE unity rb_sio)
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

# rb_application
rb_add_host_test(test_image_location application/test_image_location.cpp)
rb_add_host_test(test_port_contracts application/test_port_contracts.cpp)
rb_add_host_test(test_drive_mailbox application/test_drive_mailbox.cpp)
rb_add_host_test(test_drive_bay application/test_drive_bay.cpp)
rb_add_host_test(test_drive_lifecycle application/test_drive_lifecycle.cpp)
rb_add_abort_test(test_drive_bay_invalid_count_aborts test_drive_bay --invalid-drive-count)
rb_add_abort_test(test_repository_close_attached_aborts test_port_contracts --close-attached-source)
rb_add_abort_test(test_mailbox_reply_without_request_aborts test_drive_mailbox --reply-without-request)
