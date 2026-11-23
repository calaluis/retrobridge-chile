// NetRestoreQueue (menu-tnfs-persistente T2, AC4 and AC6): the network drives come back one at a
// time; deadlines by bytes received and a monotonic clock, cancel and requeue.

#include <cstdint>

#include "rb/menu/drive_memory.hpp"
#include "rb/menu/net_restore_queue.hpp"
#include "unity.h"

using rb::host::DriveMemory;
using rb::host::NetRestoreQueue;
using State = rb::host::NetRestoreQueue::State;
using Reason = rb::host::NetRestoreQueue::Reason;

void setUp(void) {}
void tearDown(void) {}

namespace {

constexpr uint64_t kBoot = 1000;  // the clock does not start at 0

// D1 local, D2 and D4 from the network, D3 empty. A member of no task: static, like the firmware's.
const DriveMemory& memory()
{
    static DriveMemory m;
    m.clear();
    TEST_ASSERT_TRUE(m.set(1, "DOS.ATR"));
    TEST_ASSERT_TRUE(m.setNet(2, DriveMemory::Origin::NetAtr, "srv-a", "/juegos/ARCADE.ATR", "ARCADE.ATR"));
    TEST_ASSERT_TRUE(m.setNet(4, DriveMemory::Origin::NetXex, "srv-a", "/xex/JUEGO.XEX", "JUEGO.XEX"));
    return m;
}

// The queue after begin() with the network Ready and D2 Loading as job 7 since `kBoot`.
NetRestoreQueue loadingD2()
{
    NetRestoreQueue q;
    q.begin(memory(), kBoot);
    TEST_ASSERT_EQUAL_UINT(2, q.next(true, kBoot));
    TEST_ASSERT_TRUE(q.started(2, 7, kBoot));
    return q;
}

}  // namespace

static void test_begin_takes_only_the_network_drives(void)
{
    NetRestoreQueue q;
    q.begin(memory(), kBoot);
    TEST_ASSERT_EQUAL(State::None, q.state(1));
    TEST_ASSERT_EQUAL(State::Pending, q.state(2));
    TEST_ASSERT_EQUAL(State::None, q.state(3));
    TEST_ASSERT_EQUAL(State::Pending, q.state(4));
    TEST_ASSERT_EQUAL(State::None, q.state(0));
    TEST_ASSERT_EQUAL(State::None, q.state(9));
    TEST_ASSERT_TRUE(q.busy());

    NetRestoreQueue local;
    DriveMemory onlyLocal;
    TEST_ASSERT_TRUE(onlyLocal.set(1, "DOS.ATR"));
    local.begin(onlyLocal, kBoot);
    TEST_ASSERT_FALSE(local.busy());
    TEST_ASSERT_EQUAL_UINT(0, local.next(true, kBoot));
}

static void test_one_drive_at_a_time_and_only_with_the_network(void)
{
    NetRestoreQueue q;
    q.begin(memory(), kBoot);
    TEST_ASSERT_EQUAL_UINT(0, q.next(false, kBoot));  // no network: nothing asked for
    TEST_ASSERT_EQUAL_UINT(2, q.next(true, kBoot));    // the lowest Pending
    TEST_ASSERT_FALSE(q.started(2, 0, kBoot));         // job 0 is no job
    TEST_ASSERT_FALSE(q.started(3, 8, kBoot));         // D3 is not Pending
    TEST_ASSERT_TRUE(q.started(2, 7, kBoot));
    TEST_ASSERT_FALSE(q.started(2, 9, kBoot));         // already Loading
    TEST_ASSERT_EQUAL(State::Loading, q.state(2));
    TEST_ASSERT_EQUAL_UINT32(7, q.jobOf(2));
    TEST_ASSERT_EQUAL_UINT(2, q.driveOf(7));
    TEST_ASSERT_EQUAL_UINT(0, q.next(true, kBoot));    // D4 waits for D2

    TEST_ASSERT_TRUE(q.done(2));
    TEST_ASSERT_EQUAL(State::Mounted, q.state(2));
    TEST_ASSERT_EQUAL_UINT(0, q.driveOf(7));
    TEST_ASSERT_FALSE(q.done(2));                      // a second result changes nothing
    TEST_ASSERT_EQUAL_UINT(4, q.next(true, kBoot));
    TEST_ASSERT_TRUE(q.started(4, 8, kBoot));
    TEST_ASSERT_TRUE(q.failed(4, Reason::Falta));
    TEST_ASSERT_EQUAL(State::Failed, q.state(4));
    TEST_ASSERT_EQUAL(Reason::Falta, q.reason(4));
    TEST_ASSERT_FALSE(q.busy());
    TEST_ASSERT_EQUAL_UINT(0, q.next(true, kBoot));
}

// AC4: no Wi-Fi Ready 30 s after power-up -> SIN RED, for every drive still waiting.
static void test_no_wifi_in_30_s_fails_the_pending_drives(void)
{
    NetRestoreQueue q;
    q.begin(memory(), kBoot);
    TEST_ASSERT_EQUAL_UINT(0, q.next(false, kBoot + 29999));
    TEST_ASSERT_EQUAL_HEX8(0, q.expired(kBoot + 29999));
    TEST_ASSERT_EQUAL_HEX8(0x0A, q.expired(kBoot + 30000));  // D2 and D4
    TEST_ASSERT_EQUAL(State::Failed, q.state(2));
    TEST_ASSERT_EQUAL(Reason::SinRed, q.reason(2));
    TEST_ASSERT_EQUAL(Reason::SinRed, q.reason(4));
    TEST_ASSERT_EQUAL_HEX8(0, q.expired(kBoot + 60000));    // reported once
    TEST_ASSERT_EQUAL_UINT(0, q.next(true, kBoot + 60000));
}

// Review of af5974c: the network turns Ready only after 30 s. Both orders of next() and expired()
// end the same way: SIN RED, nothing asked for.
static void test_wifi_ready_too_late_fails_in_either_order(void)
{
    NetRestoreQueue first;  // next() first
    first.begin(memory(), kBoot);
    TEST_ASSERT_EQUAL_UINT(0, first.next(true, kBoot + 30000));
    TEST_ASSERT_EQUAL(State::Pending, first.state(2));  // not started
    TEST_ASSERT_EQUAL_HEX8(0x0A, first.expired(kBoot + 30000));
    TEST_ASSERT_EQUAL(Reason::SinRed, first.reason(2));
    TEST_ASSERT_EQUAL_UINT(0, first.next(true, kBoot + 30001));

    NetRestoreQueue second;  // expired() first
    second.begin(memory(), kBoot);
    TEST_ASSERT_EQUAL_HEX8(0x0A, second.expired(kBoot + 45000));
    TEST_ASSERT_EQUAL_UINT(0, second.next(true, kBoot + 45000));
    TEST_ASSERT_EQUAL(Reason::SinRed, second.reason(4));

    NetRestoreQueue inTime;  // Ready 1 ms before the deadline: it goes on, with no Wi-Fi deadline
    inTime.begin(memory(), kBoot);
    TEST_ASSERT_EQUAL_UINT(2, inTime.next(true, kBoot + 29999));
    TEST_ASSERT_EQUAL_HEX8(0, inTime.expired(kBoot + 29999));
    TEST_ASSERT_TRUE(inTime.started(2, 7, kBoot + 29999));
    TEST_ASSERT_EQUAL_HEX8(0, inTime.expired(kBoot + 31000));  // D4 waits: no deadline any more
}

// Once the network was Ready, a drive waiting behind another (or behind the user) has no deadline.
static void test_after_the_network_was_ready_waiting_is_not_a_failure(void)
{
    NetRestoreQueue q = loadingD2();
    for (uint64_t t = kBoot; t <= kBoot + 120000; t += 10000) {
        q.progressed(2, static_cast<uint32_t>(t - kBoot + 1), t);
        TEST_ASSERT_EQUAL_HEX8(0, q.expired(t));
    }
    TEST_ASSERT_EQUAL(State::Pending, q.state(4));
}

// AC4: the server never answers -> SIN RED 30 s after the job started.
static void test_no_first_response_in_30_s(void)
{
    NetRestoreQueue q = loadingD2();
    q.progressed(2, 0, kBoot + 20000);  // no bytes is no data
    TEST_ASSERT_EQUAL_HEX8(0, q.expired(kBoot + 29999));
    TEST_ASSERT_EQUAL_HEX8(0x02, q.expired(kBoot + 30000));
    TEST_ASSERT_EQUAL(Reason::SinRed, q.reason(2));
    TEST_ASSERT_EQUAL_UINT32(7, q.jobOf(2));  // the caller cancels it
    TEST_ASSERT_EQUAL_UINT(0, q.driveOf(7));  // and its late result is no one's
    TEST_ASSERT_EQUAL_UINT(4, q.next(true, kBoot + 30000));
}

// AC4 (a): a block every 20 s for 2 minutes; the integer percent never moves. It does not expire.
static void test_slow_progress_does_not_expire(void)
{
    NetRestoreQueue q = loadingD2();
    uint32_t bytes = 0;
    for (uint64_t t = kBoot + 20000; t <= kBoot + 120000; t += 20000) {
        bytes += 128;  // 128 B of a 92 160 B ATR: 0 % all the way
        q.progressed(2, bytes, t);
        TEST_ASSERT_EQUAL_HEX8(0, q.expired(t + 19999));
    }
    TEST_ASSERT_EQUAL(State::Loading, q.state(2));
    TEST_ASSERT_EQUAL_HEX8(0x02, q.expired(kBoot + 120000 + 30000));  // then it stops
}

// AC4 (b): a 31 s pause in the middle -> SIN RED.
static void test_a_31_s_pause_in_the_middle(void)
{
    NetRestoreQueue q = loadingD2();
    q.progressed(2, 40000, kBoot + 5000);
    q.progressed(2, 40000, kBoot + 20000);  // the same count again is not data
    TEST_ASSERT_EQUAL_HEX8(0, q.expired(kBoot + 5000 + 29999));
    TEST_ASSERT_EQUAL_HEX8(0x02, q.expired(kBoot + 5000 + 31000));
    TEST_ASSERT_EQUAL(Reason::SinRed, q.reason(2));
}

// AC4 (c): the server stops after the first block -> SIN RED 30 s after that block.
static void test_a_server_stopped_after_the_first_block(void)
{
    NetRestoreQueue q = loadingD2();
    q.progressed(2, 512, kBoot + 3000);
    TEST_ASSERT_EQUAL_HEX8(0, q.expired(kBoot + 32999));
    TEST_ASSERT_EQUAL_HEX8(0x02, q.expired(kBoot + 33000));
}

// A clock that reads earlier is not elapsed time (never negative, never a false expiry).
static void test_a_clock_that_reads_earlier_is_no_time(void)
{
    NetRestoreQueue q = loadingD2();
    q.progressed(2, 512, kBoot + 3000);
    TEST_ASSERT_EQUAL_HEX8(0, q.expired(kBoot));
    TEST_ASSERT_EQUAL_HEX8(0, q.expired(0));
}

// AC6: the user changes a Loading drive; the late result of its job is no one's.
static void test_cancel_drops_the_drive_and_its_late_result(void)
{
    NetRestoreQueue q = loadingD2();
    q.cancel(2);
    TEST_ASSERT_EQUAL(State::None, q.state(2));
    TEST_ASSERT_EQUAL_UINT(0, q.driveOf(7));
    TEST_ASSERT_FALSE(q.done(2));
    TEST_ASSERT_FALSE(q.failed(2, Reason::NoCabe));
    TEST_ASSERT_EQUAL_HEX8(0, q.expired(kBoot + 60000));
    TEST_ASSERT_EQUAL_UINT(4, q.next(true, kBoot + 60000));  // the next one goes on
    q.cancel(4);                                              // a Pending one leaves too
    TEST_ASSERT_FALSE(q.busy());
    q.cancel(0);
    q.cancel(9);
}

// A user's job takes the loader: the drive goes back to Pending and starts afresh later.
static void test_requeue_starts_the_deadlines_afresh(void)
{
    NetRestoreQueue q = loadingD2();
    q.progressed(2, 4096, kBoot + 1000);
    TEST_ASSERT_TRUE(q.requeue(2));
    TEST_ASSERT_FALSE(q.requeue(2));  // not Loading any more
    TEST_ASSERT_EQUAL(State::Pending, q.state(2));
    TEST_ASSERT_EQUAL_UINT(0, q.driveOf(7));
    TEST_ASSERT_EQUAL_HEX8(0, q.expired(kBoot + 100000));  // a waiting drive does not expire
    TEST_ASSERT_EQUAL_UINT(2, q.next(true, kBoot + 100000));
    TEST_ASSERT_TRUE(q.started(2, 11, kBoot + 100000));
    q.progressed(2, 128, kBoot + 101000);  // fewer bytes than before: a new job, new count
    TEST_ASSERT_EQUAL_HEX8(0, q.expired(kBoot + 130999));
    TEST_ASSERT_EQUAL_HEX8(0x02, q.expired(kBoot + 131000));
}

static void test_results_for_drives_out_of_range_or_not_loading_are_ignored(void)
{
    NetRestoreQueue q = loadingD2();
    TEST_ASSERT_FALSE(q.done(0));
    TEST_ASSERT_FALSE(q.done(9));
    TEST_ASSERT_FALSE(q.done(4));                        // Pending, not Loading
    TEST_ASSERT_FALSE(q.failed(2, Reason::None));        // a failure has a reason
    TEST_ASSERT_FALSE(q.failed(4, Reason::NoSePudoAbrir));
    q.progressed(4, 100, kBoot);
    q.progressed(9, 100, kBoot);
    TEST_ASSERT_EQUAL_UINT32(0, q.jobOf(9));
    TEST_ASSERT_EQUAL_UINT(0, q.driveOf(0));
    TEST_ASSERT_EQUAL(State::Loading, q.state(2));
}

static void test_begin_again_starts_over(void)
{
    NetRestoreQueue q = loadingD2();
    q.begin(memory(), kBoot + 50000);
    TEST_ASSERT_EQUAL(State::Pending, q.state(2));
    TEST_ASSERT_EQUAL_UINT(0, q.driveOf(7));
    TEST_ASSERT_EQUAL_HEX8(0, q.expired(kBoot + 79999));  // the Wi-Fi deadline counts again
    TEST_ASSERT_EQUAL_HEX8(0x0A, q.expired(kBoot + 80000));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_begin_takes_only_the_network_drives);
    RUN_TEST(test_one_drive_at_a_time_and_only_with_the_network);
    RUN_TEST(test_no_wifi_in_30_s_fails_the_pending_drives);
    RUN_TEST(test_wifi_ready_too_late_fails_in_either_order);
    RUN_TEST(test_after_the_network_was_ready_waiting_is_not_a_failure);
    RUN_TEST(test_no_first_response_in_30_s);
    RUN_TEST(test_slow_progress_does_not_expire);
    RUN_TEST(test_a_31_s_pause_in_the_middle);
    RUN_TEST(test_a_server_stopped_after_the_first_block);
    RUN_TEST(test_a_clock_that_reads_earlier_is_no_time);
    RUN_TEST(test_cancel_drops_the_drive_and_its_late_result);
    RUN_TEST(test_requeue_starts_the_deadlines_afresh);
    RUN_TEST(test_results_for_drives_out_of_range_or_not_loading_are_ignored);
    RUN_TEST(test_begin_again_starts_over);
    return UNITY_END();
}
