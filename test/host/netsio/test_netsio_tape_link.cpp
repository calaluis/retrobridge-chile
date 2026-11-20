// NetSioTapeLink (spec cassette-cas, C002): MOTOR, rate and tape bytes over NetSIO,
// and no tape bytes while a disk command waits for its answer.

#include "netsio_harness.hpp"
#include "netsio_tape_link.hpp"
#include "unity.h"

using rb::core::Span;
using rb::host::NetSioTapeLink;
using rb::test::NetSioHarness;

void setUp(void) {}
void tearDown(void) {}

static void test_motor_and_rate_go_through(void)
{
    NetSioHarness h;
    h.connect();
    NetSioTapeLink link{h.transport};
    TEST_ASSERT_FALSE(link.motorOn());
    h.from_atari({0x21});
    (void)h.session.step();
    TEST_ASSERT_TRUE(link.motorOn());
    TEST_ASSERT_TRUE(link.setBaud(600).ok());
    h.assert_sent(0, {0x80, 0x58, 0x02, 0x00, 0x00});
}

static void test_tape_bytes_go_out_as_a_data_block(void)
{
    NetSioHarness h;
    h.connect();
    NetSioTapeLink link{h.transport};
    (void)h.session.step();  // takes the hub's credit (C7) queued by connect()
    TEST_ASSERT_EQUAL_UINT8(3, h.transport.credit());
    const uint8_t bytes[] = {0x55, 0x55, 0xFC};
    TEST_ASSERT_TRUE(link.sendTapeBytes(Span<const uint8_t>{bytes}).ok());
    h.assert_sent(0, {0x02, 0x55, 0x55, 0xFC});
}

static void test_tape_bytes_are_refused_while_a_sync_is_pending(void)
{
    NetSioHarness h;
    h.connect();
    NetSioTapeLink link{h.transport};
    h.push_frame({0x31, 0x53, 0x00, 0x00, 0x84}, 0x0F);  // STATUS D1, its sync waits for an answer
    TEST_ASSERT_TRUE(h.transport.waitCommandFrame(1000000).ok());
    TEST_ASSERT_TRUE(h.transport.syncPending());
    const uint8_t bytes[] = {0x55};
    TEST_ASSERT_FALSE(link.sendTapeBytes(Span<const uint8_t>{bytes}).ok());
    TEST_ASSERT_EQUAL_size_t(0, h.port.sent_count());
    TEST_ASSERT_TRUE(h.transport.syncPending());  // the disk's answer is still owed
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_motor_and_rate_go_through);
    RUN_TEST(test_tape_bytes_go_out_as_a_data_block);
    RUN_TEST(test_tape_bytes_are_refused_while_a_sync_is_pending);
    return UNITY_END();
}
