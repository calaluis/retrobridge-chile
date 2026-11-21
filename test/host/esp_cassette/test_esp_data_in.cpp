// rb_esp_cassette: EspDataIn hands DATA IN (GPIO21) between UART2 and the RMT (cassette-cas C107,
// H4-C C8). Runs the real driver against a scripted model of the ESP-IDF calls (fakes/idf).
// What it proves: who owns the pin after each step, and what the driver does when giving the
// pin back to the UART fails. What it cannot prove: electrical levels on the SIO bus, timing,
// or that the real IDF behaves as the model. That is the physical check of DATA IN, still pending.

#include <cstdint>
#include <new>

#include "fakes/idf/fake_idf.hpp"
#include "rb/board/board_profile.hpp"
#include "rb/board/fujinet_atari_v1x.hpp"
#include "rb/esp/esp_data_in.hpp"
#include "unity.h"

using fake_idf::Owner;
using rb::board::BoardProfile;
using rb::cassette::FskPortion;
using rb::esp::EspDataIn;

namespace {

// A test-only copy with both gates signed: nothing here approves H4 or M3a.
rb::board::CassetteOutputAssignment granted()
{
    BoardProfile p = rb::board::fujinet_atari_v1x_profile();
    p.sioGateApproved = true;
    p.motorGateApproved = true;
    return rb::board::validate_for_cassette_output(p).value();
}

// EspDataIn holds a 16 KB feed: one static buffer, a fresh driver built in it for each test.
alignas(EspDataIn) unsigned char g_storage[sizeof(EspDataIn)];
EspDataIn* g_dataIn = nullptr;

void configured()
{
    g_dataIn = new (g_storage) EspDataIn();
    TEST_ASSERT_TRUE(g_dataIn->configure(granted()));
}

// Queues a short run and lets pump() hand the pin to the RMT and start it.
void startRun()
{
    TEST_ASSERT_TRUE(g_dataIn->push(FskPortion{false, 200}));
    TEST_ASSERT_TRUE(g_dataIn->push(FskPortion{true, 200}));
    g_dataIn->pump();  // size grew from 0: waits one tick for more
    g_dataIn->pump();  // no growth: the run starts
    TEST_ASSERT_TRUE(g_dataIn->fskActive());
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Rmt);
}

void expectFaulted()
{
    TEST_ASSERT_TRUE(g_dataIn->faulted());
    TEST_ASSERT_FALSE(g_dataIn->fskActive());
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Gpio);  // safe state: output off, pull-up
    TEST_ASSERT_TRUE(fake_idf::line_released());
    TEST_ASSERT_TRUE(fake_idf::state().errorLogs > 0);
}

void expectRefusesEverything()
{
    const uint8_t byte = 0x55;
    const auto before = fake_idf::state().written.size();
    TEST_ASSERT_FALSE(g_dataIn->send(rb::core::Span<const uint8_t>(&byte, 1)));
    TEST_ASSERT_EQUAL_UINT32(before, fake_idf::state().written.size());
    TEST_ASSERT_FALSE(g_dataIn->setBaud(1200));
    TEST_ASSERT_FALSE(g_dataIn->push(FskPortion{false, 200}));
    const unsigned channels = fake_idf::state().rmtNewChannels;
    g_dataIn->pump();
    g_dataIn->pump();
    TEST_ASSERT_EQUAL_UINT(channels, fake_idf::state().rmtNewChannels);  // the RMT is not retried
    TEST_ASSERT_TRUE(g_dataIn->idle());  // the player sees nothing pending and does not wait
}

}  // namespace

void setUp() { fake_idf::reset(); }
void tearDown() {}

// --- The normal hand-over (regression) ---

static void test_configure_gives_the_pin_to_the_uart(void)
{
    configured();
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Uart);
    TEST_ASSERT_EQUAL_INT(21, fake_idf::state().pin);
    TEST_ASSERT_FALSE(g_dataIn->faulted());
}

static void test_a_run_starts_and_ends_at_mark(void)
{
    configured();
    startRun();
    TEST_ASSERT_EQUAL_UINT32(1, fake_idf::state().initLevel);
    TEST_ASSERT_EQUAL_UINT32(1, fake_idf::state().eotLevel);
    TEST_ASSERT_TRUE(fake_idf::play_all() > 0);
    g_dataIn->pump();  // the run has played: the pin goes back to the UART
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Uart);
    TEST_ASSERT_FALSE(fake_idf::state().rmtChannel);
    TEST_ASSERT_FALSE(g_dataIn->faulted());
    TEST_ASSERT_TRUE(g_dataIn->idle());
    const uint8_t byte = 0xA5;
    TEST_ASSERT_TRUE(g_dataIn->send(rb::core::Span<const uint8_t>(&byte, 1)));
}

static void test_stop_mid_run_gives_the_pin_back_to_the_uart(void)
{
    configured();
    startRun();
    g_dataIn->stop();  // MOTOR dropped or `cas eject`
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Uart);
    TEST_ASSERT_TRUE(fake_idf::line_released());
    TEST_ASSERT_FALSE(g_dataIn->faulted());
}

// --- uart_set_pin() fails when the pin comes back from the RMT ---

static void test_release_failure_after_a_run_is_reported_and_made_safe(void)
{
    configured();
    startRun();
    (void)fake_idf::play_all();
    fake_idf::state().failUartSetPin = true;
    g_dataIn->pump();
    expectFaulted();
    TEST_ASSERT_EQUAL_UINT32(1, g_dataIn->pinFaults());
    expectRefusesEverything();
}

static void test_release_failure_on_stop_is_reported_and_made_safe(void)
{
    configured();
    startRun();
    fake_idf::state().failUartSetPin = true;
    g_dataIn->stop();
    TEST_ASSERT_FALSE(fake_idf::state().rmtChannel);
    expectFaulted();
    expectRefusesEverything();
}

static void test_release_failure_after_rmt_enable_fails_is_reported_and_made_safe(void)
{
    configured();
    fake_idf::state().failRmtEnable = true;
    fake_idf::state().failUartSetPin = true;
    TEST_ASSERT_TRUE(g_dataIn->push(FskPortion{false, 200}));
    g_dataIn->pump();
    g_dataIn->pump();  // takePin: channel made, enable fails, the UART does not take the pin back
    TEST_ASSERT_FALSE(fake_idf::state().rmtChannel);
    expectFaulted();
    TEST_ASSERT_EQUAL_UINT32(1, g_dataIn->failures());  // the run was dropped too
    expectRefusesEverything();
}

static void test_a_failed_safe_state_is_still_a_fault(void)
{
    configured();
    startRun();
    fake_idf::state().failUartSetPin = true;
    fake_idf::state().failGpioReset = true;
    g_dataIn->stop();
    TEST_ASSERT_TRUE(g_dataIn->faulted());
    TEST_ASSERT_TRUE(fake_idf::state().gpioResets > 0);
    expectRefusesEverything();
}

// --- Explicit recovery ---

static void test_recover_keeps_the_fault_while_the_uart_still_fails(void)
{
    configured();
    startRun();
    fake_idf::state().failUartSetPin = true;
    g_dataIn->stop();
    TEST_ASSERT_FALSE(g_dataIn->recover());
    TEST_ASSERT_TRUE(g_dataIn->faulted());
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Gpio);
    expectRefusesEverything();
}

static void test_recover_gives_the_pin_back_to_the_uart_and_accepts_again(void)
{
    configured();
    startRun();
    fake_idf::state().failUartSetPin = true;
    g_dataIn->stop();
    fake_idf::state().failUartSetPin = false;
    TEST_ASSERT_TRUE(g_dataIn->recover());
    TEST_ASSERT_FALSE(g_dataIn->faulted());
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Uart);
    const uint8_t byte = 0x3C;
    TEST_ASSERT_TRUE(g_dataIn->send(rb::core::Span<const uint8_t>(&byte, 1)));
    TEST_ASSERT_EQUAL_UINT8(0x3C, fake_idf::state().written.back());
    startRun();  // the RMT works again
}

static void test_recover_without_a_fault_changes_nothing(void)
{
    configured();
    const unsigned calls = fake_idf::state().uartSetPinCalls;
    TEST_ASSERT_TRUE(g_dataIn->recover());
    TEST_ASSERT_EQUAL_UINT(calls, fake_idf::state().uartSetPinCalls);
}

// --- Bytes already queued when MOTOR drops (C8) ---

static void test_stop_leaves_bytes_already_handed_to_the_uart(void)
{
    // The driver does not flush the UART: a record's bytes already given to uart_write_bytes go
    // out, then the line idles at mark. stop() only concerns FSK runs.
    configured();
    const uint8_t bytes[3] = {1, 2, 3};
    TEST_ASSERT_TRUE(g_dataIn->send(rb::core::Span<const uint8_t>(bytes, 3)));
    g_dataIn->stop();
    TEST_ASSERT_EQUAL_UINT32(3, fake_idf::state().written.size());
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Uart);
    TEST_ASSERT_FALSE(g_dataIn->faulted());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_configure_gives_the_pin_to_the_uart);
    RUN_TEST(test_a_run_starts_and_ends_at_mark);
    RUN_TEST(test_stop_mid_run_gives_the_pin_back_to_the_uart);
    RUN_TEST(test_release_failure_after_a_run_is_reported_and_made_safe);
    RUN_TEST(test_release_failure_on_stop_is_reported_and_made_safe);
    RUN_TEST(test_release_failure_after_rmt_enable_fails_is_reported_and_made_safe);
    RUN_TEST(test_a_failed_safe_state_is_still_a_fault);
    RUN_TEST(test_recover_keeps_the_fault_while_the_uart_still_fails);
    RUN_TEST(test_recover_gives_the_pin_back_to_the_uart_and_accepts_again);
    RUN_TEST(test_recover_without_a_fault_changes_nothing);
    RUN_TEST(test_stop_leaves_bytes_already_handed_to_the_uart);
    return UNITY_END();
}
