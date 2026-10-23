// H0: proves the host test harness runs with the firmware's language flags.
// The real guarantees are compile-time (toolchain_checks.hpp); this test makes
// the result visible in CTest and confirms Unity is wired up.

#include "rb/core/toolchain_checks.hpp"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static void test_language_standard_is_cpp17(void)
{
    TEST_ASSERT_EQUAL_INT64(201703L, __cplusplus);
}

static void test_exceptions_and_rtti_are_disabled(void)
{
#if defined(__cpp_exceptions) || defined(__cpp_rtti)
    TEST_FAIL_MESSAGE("exceptions or RTTI enabled");
#else
    TEST_PASS();
#endif
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_language_standard_is_cpp17);
    RUN_TEST(test_exceptions_and_rtti_are_disabled);
    return UNITY_END();
}
