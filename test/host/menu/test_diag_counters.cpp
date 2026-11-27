// Review of PR #124 (H1): the diagnostic counters of issue #125 are written by the SIO task and read by
// the console task (`sio status`). Each one is a 32-bit std::atomic, so a read never races a write;
// the fields are independent (a reading is not one snapshot of all of them). Lock-freedom is checked
// here, on the host, as in data_in_handover.hpp: the ESP32 GCC does not report it (CI of 2da5b81).
#include <atomic>
#include <cstdint>
#include <type_traits>

#include "rb/esp/esp_sio_uart.hpp"
#include "rb/menu/menu_sio_transport.hpp"

#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

using Counter = std::atomic<uint32_t>;
using rb::esp::EspSioUart;
using rb::host::MenuTransportStats;

static void test_the_menu_counters_read_by_the_console_are_atomic(void)
{
    TEST_ASSERT_TRUE((std::is_same<decltype(MenuTransportStats::menuBadChecksums), Counter>::value));
    TEST_ASSERT_TRUE((std::is_same<decltype(MenuTransportStats::menuSendFailures), Counter>::value));
    TEST_ASSERT_TRUE((std::is_same<decltype(MenuTransportStats::menuAckFailures), Counter>::value));
    TEST_ASSERT_TRUE((std::is_same<decltype(MenuTransportStats::lastReplyUs), Counter>::value));
    TEST_ASSERT_TRUE((std::is_same<decltype(MenuTransportStats::lastInterruptedUs), Counter>::value));
    TEST_ASSERT_TRUE((std::is_same<decltype(MenuTransportStats::maxReplyUs), Counter>::value));
}

static void test_the_uart_write_failures_read_by_the_console_are_atomic(void)
{
    using W = EspSioUart::WriteFailures;
    TEST_ASSERT_TRUE((std::is_same<decltype(W::notReady), Counter>::value));
    TEST_ASSERT_TRUE((std::is_same<decltype(W::shortWrite), Counter>::value));
    TEST_ASSERT_TRUE((std::is_same<decltype(W::txNotDone), Counter>::value));
    TEST_ASSERT_TRUE((std::is_same<decltype(W::lastBytes), Counter>::value));
    TEST_ASSERT_TRUE((std::is_same<decltype(W::lastWaitMs), Counter>::value));
    TEST_ASSERT_TRUE((std::is_same<decltype(W::maxWriteMs), Counter>::value));
}

static void test_the_counters_are_lock_free_on_the_host(void) { TEST_ASSERT_TRUE(Counter::is_always_lock_free); }

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_the_menu_counters_read_by_the_console_are_atomic);
    RUN_TEST(test_the_uart_write_failures_read_by_the_console_are_atomic);
    RUN_TEST(test_the_counters_are_lock_free_on_the_host);
    return UNITY_END();
}
