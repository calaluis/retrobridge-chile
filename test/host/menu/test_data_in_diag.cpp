// TakeDiag (main/data_in_diag.hpp, fix-cinta-data-in-65xe, review PR #124 H1): what the cassette did
// for ONE take of DATA IN, from the hardwareFree() counters taken when the take was asked and when it
// ended. A counter left over from an earlier take must not be read as this take's answer.

#include "data_in_diag.hpp"
#include "unity.h"

using rb::menusio::HardwareProbe;
using rb::menusio::TakeDiag;
using rb::menusio::TakeVerdict;

void setUp() {}
void tearDown() {}

// A: no hardwareFree() call during the take. An old busy reading (0x40) is not this take's answer.
static void test_a_take_with_no_new_check_says_not_checked(void)
{
    TakeDiag diag;
    diag.requested(1000, HardwareProbe{7, 3, 0x40});
    const TakeDiag::Result r = diag.ended(21000, HardwareProbe{7, 3, 0x40});
    TEST_ASSERT_TRUE(r.hasRequest);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(TakeVerdict::NotChecked), static_cast<int>(r.verdict));
    TEST_ASSERT_EQUAL_UINT32(0, r.calls);
    TEST_ASSERT_EQUAL_INT64(20000, r.elapsedUs);
}

// A, the other face: an old free reading (0) with no new check is not "free but unconfirmed" either.
static void test_an_old_free_reading_is_not_this_takes_answer(void)
{
    TakeDiag diag;
    diag.requested(0, HardwareProbe{5, 0, 0});
    TEST_ASSERT_EQUAL_INT(static_cast<int>(TakeVerdict::NotChecked),
                          static_cast<int>(diag.ended(20000, HardwareProbe{5, 0, 0}).verdict));
}

// B: checks during the take, the last one busy: the bits are this take's.
static void test_b_checks_with_a_busy_bit_say_busy(void)
{
    TakeDiag diag;
    diag.requested(0, HardwareProbe{10, 4, 0});
    const TakeDiag::Result r = diag.ended(20000, HardwareProbe{19, 13, 0x40});
    TEST_ASSERT_EQUAL_INT(static_cast<int>(TakeVerdict::Busy), static_cast<int>(r.verdict));
    TEST_ASSERT_EQUAL_UINT32(9, r.calls);
    TEST_ASSERT_EQUAL_UINT32(9, r.refusals);
    TEST_ASSERT_EQUAL_UINT32(0x40, r.lastBusy);
}

// C: checks during the take, the last one free, and still no confirmation.
static void test_c_free_checks_without_confirmation_say_free_not_confirmed(void)
{
    TakeDiag diag;
    diag.requested(0, HardwareProbe{10, 4, 0x40});
    const TakeDiag::Result r = diag.ended(20000, HardwareProbe{12, 5, 0});
    TEST_ASSERT_EQUAL_INT(static_cast<int>(TakeVerdict::FreeNotConfirmed), static_cast<int>(r.verdict));
    TEST_ASSERT_EQUAL_UINT32(2, r.calls);
    TEST_ASSERT_EQUAL_UINT32(1, r.refusals);
}

// ToTape in the same arbiter step as the take (take() returned Done): no request was seen, so no time.
static void test_an_end_without_a_request_has_no_elapsed_time(void)
{
    TakeDiag diag;
    const TakeDiag::Result r = diag.ended(5000, HardwareProbe{1, 0, 0});
    TEST_ASSERT_FALSE(r.hasRequest);
}

// One request, one end: a second end without a new request has no request either.
static void test_each_end_consumes_its_request(void)
{
    TakeDiag diag;
    diag.requested(0, HardwareProbe{});
    TEST_ASSERT_TRUE(diag.ended(100, HardwareProbe{}).hasRequest);
    TEST_ASSERT_FALSE(diag.ended(200, HardwareProbe{}).hasRequest);
}

// The counters wrap at 2^32: deltas are modulo, never a huge number.
static void test_counters_that_wrap_give_small_deltas(void)
{
    TakeDiag diag;
    diag.requested(0, HardwareProbe{0xFFFFFFFEu, 0xFFFFFFFFu, 0});
    const TakeDiag::Result r = diag.ended(1, HardwareProbe{1, 0, 0x40});
    TEST_ASSERT_EQUAL_UINT32(3, r.calls);
    TEST_ASSERT_EQUAL_UINT32(1, r.refusals);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(TakeVerdict::Busy), static_cast<int>(r.verdict));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_a_take_with_no_new_check_says_not_checked);
    RUN_TEST(test_an_old_free_reading_is_not_this_takes_answer);
    RUN_TEST(test_b_checks_with_a_busy_bit_say_busy);
    RUN_TEST(test_c_free_checks_without_confirmation_say_free_not_confirmed);
    RUN_TEST(test_an_end_without_a_request_has_no_elapsed_time);
    RUN_TEST(test_each_end_consumes_its_request);
    RUN_TEST(test_counters_that_wrap_give_small_deltas);
    return UNITY_END();
}
