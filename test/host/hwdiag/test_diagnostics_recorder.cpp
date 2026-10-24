// DiagnosticsRecorder: storage counters, last latencies and events.

#include "rb/hwdiag/diagnostics_recorder.hpp"
#include "unity.h"

using rb::hwdiag::BootRecord;
using rb::hwdiag::DiagEvent;
using rb::hwdiag::DiagnosticsRecorder;
using rb::hwdiag::LatencyFigures;
using rb::hwdiag::ResetReason;

void setUp(void) {}
void tearDown(void) {}

static DiagEvent last_event(const DiagnosticsRecorder& r)
{
    return r.events().at(r.events().size() - 1).event;
}

static void test_starts_empty(void)
{
    DiagnosticsRecorder r{5000};
    TEST_ASSERT_FALSE(r.storage().mounted);
    TEST_ASSERT_EQUAL_UINT32(0, r.storage().mountAttempts);
    TEST_ASSERT_EQUAL_UINT32(5000, r.storage().latencyBudgetUs);
    TEST_ASSERT_FALSE(r.storage().hasLatency);
    TEST_ASSERT_EQUAL_UINT32(0, r.events().size());
}

static void test_boot_is_an_event_with_the_boot_count(void)
{
    DiagnosticsRecorder r{5000};
    r.recordBoot(BootRecord{ResetReason::TaskWatchdog, 3, 1, 0, 0}, 42);
    TEST_ASSERT_EQUAL_UINT32(1, r.events().size());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagEvent::Boot), static_cast<int>(last_event(r)));
    TEST_ASSERT_EQUAL_UINT32(3, r.events().at(0).value);
}

static void test_mount_success_and_failure(void)
{
    DiagnosticsRecorder r{5000};
    r.recordMount(false, 0x107, 40, 10);
    TEST_ASSERT_FALSE(r.storage().mounted);
    TEST_ASSERT_EQUAL_UINT32(1, r.storage().mountFailures);
    TEST_ASSERT_EQUAL_INT32(0x107, r.storage().lastMountEspError);
    TEST_ASSERT_EQUAL_UINT8(40, r.storage().lastErrorCode);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagEvent::StorageMountFailed), static_cast<int>(last_event(r)));

    r.recordMount(true, 0, 0, 20);
    TEST_ASSERT_TRUE(r.storage().mounted);
    TEST_ASSERT_EQUAL_UINT32(2, r.storage().mountAttempts);
    TEST_ASSERT_EQUAL_UINT32(1, r.storage().mountFailures);
    TEST_ASSERT_EQUAL_UINT8(40, r.storage().lastErrorCode);  // a success keeps the last error

    r.recordUnmount(30);
    TEST_ASSERT_FALSE(r.storage().mounted);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagEvent::StorageUnmounted), static_cast<int>(last_event(r)));
}

static void test_verify_accumulates(void)
{
    DiagnosticsRecorder r{5000};
    r.recordVerify(720, 0, 0, 1);
    r.recordVerify(1040, 2, 12, 2);
    TEST_ASSERT_EQUAL_UINT32(2, r.storage().verifyRuns);
    TEST_ASSERT_EQUAL_UINT32(1760, r.storage().sectorsVerified);
    TEST_ASSERT_EQUAL_UINT32(2, r.storage().verifyReadErrors);
    TEST_ASSERT_EQUAL_UINT8(12, r.storage().lastErrorCode);
}

static void test_sector_read_error_packs_sector_and_code(void)
{
    DiagnosticsRecorder r{5000};
    r.recordSectorReadError(721, 9, 5);
    TEST_ASSERT_EQUAL_UINT32(1, r.storage().sectorReadErrors);
    TEST_ASSERT_EQUAL_UINT8(9, r.storage().lastErrorCode);
    TEST_ASSERT_EQUAL_UINT32((721u << 8) | 9u, r.events().at(0).value);
    TEST_ASSERT_EQUAL_UINT32(DiagnosticsRecorder::sector_error_value(721, 9), r.events().at(0).value);
}

static void test_benchmark_keeps_last_latency_and_flags_budget(void)
{
    DiagnosticsRecorder r{5000};
    r.recordBenchmark(LatencyFigures{1000, 0, 1062, 1845, 2048, 3024, 3110, 3953}, 1000, 0, 7);
    TEST_ASSERT_TRUE(r.storage().hasLatency);
    TEST_ASSERT_EQUAL_UINT32(3953, r.storage().lastLatency.max);
    TEST_ASSERT_EQUAL_UINT32(0, r.storage().budgetExceeded);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagEvent::BenchmarkCompleted), static_cast<int>(last_event(r)));

    r.recordBenchmark(LatencyFigures{10, 0, 100, 900, 800, 4000, 5001, 5001}, 10, 1, 8);
    TEST_ASSERT_EQUAL_UINT32(1, r.storage().budgetExceeded);
    TEST_ASSERT_EQUAL_UINT32(2, r.storage().benchmarkRuns);
    TEST_ASSERT_EQUAL_UINT32(1010, r.storage().benchmarkReads);
    TEST_ASSERT_EQUAL_UINT32(1, r.storage().benchmarkErrors);
    TEST_ASSERT_EQUAL_UINT32(5001, r.storage().lastLatency.max);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DiagEvent::LatencyBudgetExceeded), static_cast<int>(last_event(r)));
}

static void test_event_log_is_bounded(void)
{
    DiagnosticsRecorder r{5000};
    for (uint16_t s = 0; s < 100; ++s) {
        r.recordSectorReadError(s, 9, s);
    }
    TEST_ASSERT_EQUAL_UINT32(DiagnosticsRecorder::kEventCapacity, r.events().size());
    TEST_ASSERT_EQUAL_UINT32(100, r.events().total());
    TEST_ASSERT_EQUAL_UINT32(100, r.storage().sectorReadErrors);  // counters are not bounded by the log
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_starts_empty);
    RUN_TEST(test_boot_is_an_event_with_the_boot_count);
    RUN_TEST(test_mount_success_and_failure);
    RUN_TEST(test_verify_accumulates);
    RUN_TEST(test_sector_read_error_packs_sector_and_code);
    RUN_TEST(test_benchmark_keeps_last_latency_and_flags_budget);
    RUN_TEST(test_event_log_is_bounded);
    return UNITY_END();
}
