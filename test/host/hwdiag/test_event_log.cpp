// EventLog: fixed-capacity ring of the last events (no allocation).

#include "rb/hwdiag/event_log.hpp"
#include "unity.h"

using rb::hwdiag::DiagEvent;
using rb::hwdiag::EventLog;

void setUp(void) {}
void tearDown(void) {}

static void test_empty_log(void)
{
    EventLog<4> log;
    TEST_ASSERT_EQUAL_UINT32(0, log.size());
    TEST_ASSERT_EQUAL_UINT32(0, log.total());
    TEST_ASSERT_EQUAL_UINT32(0, log.overwritten());
}

static void test_records_in_chronological_order(void)
{
    EventLog<4> log;
    log.record(10, DiagEvent::Boot, 1);
    log.record(20, DiagEvent::StorageMounted, 0);
    log.record(30, DiagEvent::VerifyCompleted, 720);
    TEST_ASSERT_EQUAL_UINT32(3, log.size());
    TEST_ASSERT_EQUAL_UINT32(1, log.at(0).sequence);
    TEST_ASSERT_EQUAL_UINT64(10, log.at(0).timeUs);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagEvent::Boot), static_cast<int>(log.at(0).event));
    TEST_ASSERT_EQUAL_UINT32(720, log.at(2).value);
    TEST_ASSERT_EQUAL_UINT32(3, log.at(2).sequence);
}

static void test_full_log_overwrites_oldest_and_counts_losses(void)
{
    EventLog<3> log;
    for (uint32_t i = 1; i <= 5; ++i) {
        log.record(i * 100, DiagEvent::SectorReadError, i);
    }
    TEST_ASSERT_EQUAL_UINT32(3, log.size());
    TEST_ASSERT_EQUAL_UINT32(5, log.total());
    TEST_ASSERT_EQUAL_UINT32(2, log.overwritten());
    TEST_ASSERT_EQUAL_UINT32(3, log.at(0).sequence);  // oldest kept
    TEST_ASSERT_EQUAL_UINT32(5, log.at(2).sequence);  // newest
    TEST_ASSERT_EQUAL_UINT32(5, log.at(2).value);
}

static void test_wraps_many_times_consistently(void)
{
    EventLog<4> log;
    for (uint32_t i = 1; i <= 1003; ++i) {
        log.record(i, DiagEvent::BenchmarkCompleted, i);
    }
    TEST_ASSERT_EQUAL_UINT32(4, log.size());
    for (std::size_t k = 0; k < 4; ++k) {
        TEST_ASSERT_EQUAL_UINT32(1000 + k, log.at(k).sequence);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_log);
    RUN_TEST(test_records_in_chronological_order);
    RUN_TEST(test_full_log_overwrites_oldest_and_counts_losses);
    RUN_TEST(test_wraps_many_times_consistently);
    return UNITY_END();
}
