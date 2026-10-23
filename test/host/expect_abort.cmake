# Runs TEST_BINARY with TEST_ARG and succeeds only if it terminates abnormally
# (abort through RB_ASSERT). CTest cannot express this directly: WILL_FAIL does
# not invert a crash or signal.
#   cmake -DTEST_BINARY=<path> -DTEST_ARG=<arg> -P expect_abort.cmake
execute_process(
    COMMAND "${TEST_BINARY}" "${TEST_ARG}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_QUIET)

if(result STREQUAL "0")
    message(FATAL_ERROR "expected ${TEST_BINARY} ${TEST_ARG} to abort, but it exited normally")
endif()
message(STATUS "aborted as expected (${result})")
