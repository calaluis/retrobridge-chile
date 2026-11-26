// rb_esp_cassette: EspDataIn hands DATA IN (GPIO21) between UART2 and the RMT (cassette-cas C107,
// H4-C C8). Runs the real driver against a scripted model of the ESP-IDF calls (fakes/idf).
// What it proves: who owns the pin after each step, and what the driver does when giving the
// pin back to the UART fails. What it cannot prove: electrical levels on the SIO bus, timing,
// or that the real IDF behaves as the model. That is the physical check of DATA IN, still pending.

#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "driver/rmt_tx.h"
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

// --- Release and call trace (bench test C8) ---

namespace {
std::vector<std::string> g_trace;
void record(const char* call, int err)
{
    g_trace.push_back(std::string(call) + "=" + std::to_string(err));
}
}  // namespace

void setUp()
{
    fake_idf::reset();
    g_trace.clear();
}
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

static void test_release_after_a_run_resets_the_pin_and_disables(void)
{
    configured();
    startRun();
    (void)fake_idf::play_all();
    g_dataIn->pump();
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Uart);
    g_dataIn->release();
    TEST_ASSERT_FALSE(g_dataIn->enabled());
    TEST_ASSERT_FALSE(fake_idf::state().uartInstalled);
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Gpio);
    TEST_ASSERT_TRUE(fake_idf::line_released());
    const uint8_t byte = 1;
    TEST_ASSERT_FALSE(g_dataIn->send(rb::core::Span<const uint8_t>(&byte, 1)));
    TEST_ASSERT_FALSE(g_dataIn->push(FskPortion{false, 200}));
}

static void test_release_mid_run_stops_it_first(void)
{
    configured();
    startRun();
    g_dataIn->release();
    TEST_ASSERT_FALSE(fake_idf::state().rmtChannel);
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Gpio);
    TEST_ASSERT_FALSE(g_dataIn->enabled());
}

// analisis-h4-r2-r7 (R2): release() reports DATA IN released only when gpio_reset_pin() worked. A failed
// reset leaves the GPIO matrix as it was (U2TXD on the pin, output on) with UART2 deleted: release() says
// false, the driver stays enabled and faulted, and everything is refused until a release() that works.
static void test_release_with_a_failed_reset_is_not_released(void)
{
    configured();
    fake_idf::state().failGpioReset = true;
    TEST_ASSERT_FALSE(g_dataIn->release());
    TEST_ASSERT_TRUE(g_dataIn->enabled());
    TEST_ASSERT_TRUE(g_dataIn->faulted());
    TEST_ASSERT_FALSE(fake_idf::state().owner == Owner::Gpio);
    expectRefusesEverything();
    fake_idf::state().failGpioReset = false;
    TEST_ASSERT_TRUE(g_dataIn->release());
    TEST_ASSERT_FALSE(g_dataIn->enabled());
    TEST_ASSERT_FALSE(g_dataIn->faulted());
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Gpio);
}

// PR #113 review H1: after a release() that deleted UART2 but could not reset the pin, nothing may report DATA IN
// available again without UART2: configure() does not claim it, recover() and giveBack() keep the fault. Only a
// release() that completes ends the state, without deleting UART2 a second time; then configure() installs it again.
static void failedReleaseLeavesUartDeleted()
{
    configured();
    fake_idf::state().failGpioReset = true;
    TEST_ASSERT_FALSE(g_dataIn->release());
    TEST_ASSERT_FALSE(fake_idf::state().uartInstalled);
    fake_idf::state().failGpioReset = false;
}

static void test_configure_after_a_failed_release_does_not_claim_the_line(void)
{
    failedReleaseLeavesUartDeleted();
    TEST_ASSERT_FALSE(g_dataIn->configure(granted()));
    TEST_ASSERT_FALSE(fake_idf::state().uartInstalled);
    TEST_ASSERT_TRUE(g_dataIn->faulted());
    expectRefusesEverything();
}

static void test_recover_after_a_failed_release_keeps_the_fault(void)
{
    failedReleaseLeavesUartDeleted();
    TEST_ASSERT_FALSE(g_dataIn->recover());
    TEST_ASSERT_TRUE(g_dataIn->faulted());
    expectRefusesEverything();
}

static void test_give_back_after_a_failed_release_is_lost(void)
{
    failedReleaseLeavesUartDeleted();
    TEST_ASSERT_TRUE(g_dataIn->giveBack() == EspDataIn::GiveBack::Lost);
    TEST_ASSERT_TRUE(g_dataIn->faulted());
    TEST_ASSERT_FALSE(g_dataIn->hardwareFree());
}

static void test_a_completed_release_then_configure_installs_uart2_again(void)
{
    failedReleaseLeavesUartDeleted();
    const unsigned deletes = fake_idf::state().uartDeletes;
    TEST_ASSERT_TRUE(g_dataIn->release());
    TEST_ASSERT_EQUAL_UINT32(deletes, fake_idf::state().uartDeletes);  // UART2 not deleted a second time
    TEST_ASSERT_FALSE(g_dataIn->enabled());
    TEST_ASSERT_TRUE(g_dataIn->configure(granted()));
    TEST_ASSERT_TRUE(fake_idf::state().uartInstalled);
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Uart);
    TEST_ASSERT_FALSE(g_dataIn->faulted());
}

static void test_release_without_configure_does_nothing(void)
{
    g_dataIn = new (g_storage) EspDataIn();
    g_dataIn->release();
    TEST_ASSERT_EQUAL_UINT(0, fake_idf::state().gpioResets);
}

static void test_trace_reports_each_hand_over_call_with_its_result(void)
{
    g_dataIn = new (g_storage) EspDataIn();
    g_dataIn->setTrace(&record);
    TEST_ASSERT_TRUE(g_dataIn->configure(granted()));
    startRun();
    (void)fake_idf::play_all();
    g_dataIn->pump();
    fake_idf::state().failUartSetPin = true;  // a failure is traced with its code
    startRun();
    g_dataIn->stop();
    const std::vector<std::string> expected = {
        "uart_driver_install=0", "uart_param_config=0", "uart_set_pin=0",
        "rmt_new_tx_channel=0", "rmt_new_simple_encoder=0", "rmt_tx_register_event_callbacks=0", "rmt_enable=0",
        "rmt_transmit=0", "rmt_disable=0", "rmt_del_channel=0", "rmt_del_encoder=0", "uart_set_pin=0",
        "rmt_new_tx_channel=0", "rmt_new_simple_encoder=0", "rmt_tx_register_event_callbacks=0", "rmt_enable=0",
        "rmt_transmit=0", "rmt_disable=0", "rmt_del_channel=0", "rmt_del_encoder=0", "uart_set_pin=-1",
        "gpio_reset_pin=0"};
    TEST_ASSERT_EQUAL_UINT(expected.size(), g_trace.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        TEST_ASSERT_EQUAL_STRING(expected[i].c_str(), g_trace[i].c_str());
    }
}

// --- End of a run by the RMT's notification, one rmt_disable (bench test C8, d05bfe2) ---
// On the bench, stop() disabled the channel twice (IDF refused the second, 259) and pump() asked
// rmt_tx_wait_all_done(0) every tick (IDF logged "flush timeout" each time). The model now does both.

namespace {

std::size_t count_calls(const char* call)
{
    std::size_t n = 0;
    for (const auto& line : g_trace) {
        if (line.rfind(std::string(call) + "=", 0) == 0) {
            ++n;
        }
    }
    return n;
}

void expectEveryCallOk()
{
    for (const auto& line : g_trace) {
        TEST_ASSERT_TRUE_MESSAGE(line.size() > 2 && line.compare(line.size() - 2, 2, "=0") == 0, line.c_str());
    }
    TEST_ASSERT_EQUAL_UINT(0, fake_idf::state().rmtDisableRefused);
    TEST_ASSERT_EQUAL_UINT(0, fake_idf::state().rmtWaitTimeouts);
    TEST_ASSERT_EQUAL_UINT(0, fake_idf::state().errorLogs);
}

void tracedAndConfigured()
{
    g_dataIn = new (g_storage) EspDataIn();
    g_dataIn->setTrace(&record);
    TEST_ASSERT_TRUE(g_dataIn->configure(granted()));
}

void pumpWhilePlaying(int ticks)
{
    for (int i = 0; i < ticks; ++i) {
        g_dataIn->pump();  // the cassette loop, every 2 ms, while the RMT plays
    }
}

}  // namespace

static void test_a_run_in_flight_keeps_the_rmt_without_logging(void)
{
    tracedAndConfigured();
    startRun();
    pumpWhilePlaying(50);
    TEST_ASSERT_TRUE(g_dataIn->fskActive());
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Rmt);
    TEST_ASSERT_EQUAL_UINT(1, fake_idf::state().rmtTransmits);  // not restarted: no underrun
    TEST_ASSERT_EQUAL_UINT32(0, g_dataIn->underruns());
    expectEveryCallOk();
}

static void test_normal_end_comes_from_the_rmt_and_disables_once(void)
{
    tracedAndConfigured();
    startRun();
    pumpWhilePlaying(50);
    TEST_ASSERT_TRUE(fake_idf::play_all() > 0);  // the RMT ends the run and notifies
    g_dataIn->pump();
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Uart);
    TEST_ASSERT_FALSE(fake_idf::state().rmtChannel);
    TEST_ASSERT_TRUE(g_dataIn->idle());
    TEST_ASSERT_EQUAL_UINT(1, count_calls("rmt_disable"));
    TEST_ASSERT_EQUAL_UINT(1, count_calls("rmt_tx_register_event_callbacks"));
    expectEveryCallOk();
}

static void test_cancel_disables_once_and_gives_the_pin_back(void)
{
    tracedAndConfigured();
    startRun();
    pumpWhilePlaying(50);
    g_dataIn->stop();  // MOTOR dropped: the run in flight is cut
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Uart);
    TEST_ASSERT_FALSE(fake_idf::state().rmtChannel);
    TEST_ASSERT_TRUE(fake_idf::line_released());
    TEST_ASSERT_TRUE(g_dataIn->idle());
    TEST_ASSERT_FALSE(g_dataIn->faulted());
    TEST_ASSERT_EQUAL_UINT(1, count_calls("rmt_disable"));
    expectEveryCallOk();
}

static void test_a_new_run_after_a_cancel_starts_and_ends(void)
{
    tracedAndConfigured();
    startRun();
    pumpWhilePlaying(10);
    g_dataIn->stop();
    startRun();  // a fresh channel: the cancelled run's notification state does not carry over
    pumpWhilePlaying(10);
    TEST_ASSERT_TRUE(g_dataIn->fskActive());
    TEST_ASSERT_TRUE(fake_idf::play_all() > 0);
    g_dataIn->pump();
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Uart);
    TEST_ASSERT_TRUE(g_dataIn->idle());
    TEST_ASSERT_EQUAL_UINT(2, fake_idf::state().rmtNewChannels);
    TEST_ASSERT_EQUAL_UINT(2, fake_idf::state().rmtTransmits);
    TEST_ASSERT_EQUAL_UINT(2, count_calls("rmt_disable"));  // one per run
    TEST_ASSERT_EQUAL_UINT32(0, g_dataIn->underruns());
    expectEveryCallOk();
}

// D1 (cassette-cas-paridad). The owner's beta never resumes a run from the middle: an underrun ends it
// and the same run is played again from its start (FujiNet PR #1773, CassetteFSKStatus::underrun; PR
// #1756, cassette.cpp). Late portions after the RMT ran dry are dropped, not sent on a new
// transmission: the pin goes back to the UART at mark, and the player sees idle() mid-run and replays
// the run from its gap.
static void test_portions_after_the_rmt_ran_dry_cut_the_run(void)
{
    tracedAndConfigured();
    TEST_ASSERT_TRUE(g_dataIn->realTime());
    startRun();
    TEST_ASSERT_TRUE(fake_idf::play_all() > 0);  // the RMT ran dry and notified the end
    TEST_ASSERT_TRUE(g_dataIn->push(FskPortion{false, 200}));  // more of the run arrives late
    TEST_ASSERT_TRUE(g_dataIn->push(FskPortion{true, 200}));
    g_dataIn->pump();  // not a new transmission: the run is cut
    TEST_ASSERT_EQUAL_UINT32(1, g_dataIn->underruns());
    TEST_ASSERT_EQUAL_UINT(1, fake_idf::state().rmtTransmits);
    TEST_ASSERT_FALSE(g_dataIn->fskActive());
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Uart);
    TEST_ASSERT_TRUE(g_dataIn->idle());  // the late portions are gone
    TEST_ASSERT_EQUAL_UINT(1, count_calls("rmt_disable"));
    startRun();  // the replay starts on a fresh channel
    TEST_ASSERT_EQUAL_UINT(2, fake_idf::state().rmtNewChannels);
    TEST_ASSERT_TRUE(fake_idf::play_all() > 0);
    g_dataIn->pump();
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Uart);
    TEST_ASSERT_EQUAL_UINT32(1, g_dataIn->underruns());
    expectEveryCallOk();
}

// --- The RMT does not let go: nothing is reported as UART2's, the release completes later ---

namespace {

void expectStillTheRmts(unsigned uartSetPins)
{
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Rmt);
    TEST_ASSERT_TRUE(fake_idf::state().rmtChannel);
    TEST_ASSERT_TRUE(g_dataIn->fskActive());  // the pin is reported as the RMT's, not UART2's
    TEST_ASSERT_TRUE(g_dataIn->releasePending());
    TEST_ASSERT_FALSE(g_dataIn->idle());       // the player waits
    TEST_ASSERT_FALSE(g_dataIn->faulted());
    TEST_ASSERT_EQUAL_UINT(uartSetPins, fake_idf::state().uartSetPinCalls);  // UART2 was not given the pin
    const uint8_t byte = 0x55;
    const auto written = fake_idf::state().written.size();
    TEST_ASSERT_FALSE(g_dataIn->send(rb::core::Span<const uint8_t>(&byte, 1)));
    TEST_ASSERT_EQUAL_UINT32(written, fake_idf::state().written.size());
    TEST_ASSERT_FALSE(g_dataIn->push(FskPortion{false, 200}));  // no new run on a channel being released
}

void expectBackOnTheUart()
{
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Uart);
    TEST_ASSERT_FALSE(fake_idf::state().rmtChannel);
    TEST_ASSERT_FALSE(g_dataIn->fskActive());
    TEST_ASSERT_FALSE(g_dataIn->releasePending());
    TEST_ASSERT_TRUE(g_dataIn->idle());
    TEST_ASSERT_FALSE(g_dataIn->faulted());
    const uint8_t byte = 0x55;
    TEST_ASSERT_TRUE(g_dataIn->send(rb::core::Span<const uint8_t>(&byte, 1)));
}

}  // namespace

static void test_a_failed_disable_on_stop_keeps_the_rmt_and_completes_later(void)
{
    tracedAndConfigured();
    startRun();
    const unsigned uartSetPins = fake_idf::state().uartSetPinCalls;
    fake_idf::state().failRmtDisable = true;
    g_dataIn->stop();
    expectStillTheRmts(uartSetPins);
    TEST_ASSERT_EQUAL_UINT(0, fake_idf::state().rmtDelChannelCalls);  // not deleted while enabled
    TEST_ASSERT_EQUAL_UINT(0, fake_idf::state().rmtDelEncoderCalls);  // the RMT may still call it
    g_dataIn->pump();  // still failing: still the RMT's
    expectStillTheRmts(uartSetPins);
    fake_idf::state().failRmtDisable = false;
    g_dataIn->pump();  // the cassette loop retries
    expectBackOnTheUart();
    TEST_ASSERT_EQUAL_UINT(1, fake_idf::state().rmtDelChannelCalls);
    TEST_ASSERT_EQUAL_UINT(1, fake_idf::state().rmtDelEncoderCalls);
}

static void test_a_failed_delete_after_a_run_keeps_the_channel_and_completes_later(void)
{
    tracedAndConfigured();
    startRun();
    TEST_ASSERT_TRUE(fake_idf::play_all() > 0);
    const unsigned uartSetPins = fake_idf::state().uartSetPinCalls;
    fake_idf::state().failRmtDelChannel = true;
    g_dataIn->pump();  // the run has played, but the channel cannot be deleted
    expectStillTheRmts(uartSetPins);
    TEST_ASSERT_EQUAL_UINT(0, fake_idf::state().rmtDelEncoderCalls);  // the channel still holds it
    g_dataIn->pump();
    expectStillTheRmts(uartSetPins);
    TEST_ASSERT_EQUAL_UINT(1, count_calls("rmt_disable"));  // disabled once, not on every retry
    fake_idf::state().failRmtDelChannel = false;
    g_dataIn->pump();
    expectBackOnTheUart();
    TEST_ASSERT_EQUAL_UINT(1, fake_idf::state().rmtDelEncoderCalls);
    TEST_ASSERT_EQUAL_UINT(0, fake_idf::state().rmtDisableRefused);
}

static void test_release_with_a_failed_delete_can_be_completed_later(void)
{
    tracedAndConfigured();
    startRun();
    fake_idf::state().failRmtDelChannel = true;
    TEST_ASSERT_FALSE(g_dataIn->release());
    TEST_ASSERT_TRUE(g_dataIn->enabled());  // not released: a later release() retries
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Rmt);
    TEST_ASSERT_TRUE(fake_idf::state().uartInstalled);
    TEST_ASSERT_EQUAL_UINT(0, fake_idf::state().gpioResets);
    fake_idf::state().failRmtDelChannel = false;
    TEST_ASSERT_TRUE(g_dataIn->release());
    TEST_ASSERT_FALSE(g_dataIn->enabled());
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Gpio);
    TEST_ASSERT_FALSE(fake_idf::state().rmtChannel);
    TEST_ASSERT_FALSE(fake_idf::state().uartInstalled);
}

static void test_a_failed_disable_keeps_a_busy_queue_until_the_rmt_is_disabled(void)
{
    tracedAndConfigured();
    startRun();  // two portions queued; the RMT has not read them yet
    const unsigned uartSetPins = fake_idf::state().uartSetPinCalls;
    fake_idf::state().failRmtDisable = true;
    g_dataIn->stop();  // MOTOR dropped, but the RMT stays enabled and may still read the queue
    expectStillTheRmts(uartSetPins);
    for (int i = 0; i < 5; ++i) {
        g_dataIn->pump();  // portions left in the queue: only the release is retried
    }
    expectStillTheRmts(uartSetPins);
    TEST_ASSERT_EQUAL_UINT(1, fake_idf::state().rmtTransmits);   // no new transmission
    TEST_ASSERT_EQUAL_UINT(1, fake_idf::state().rmtNewChannels);
    TEST_ASSERT_EQUAL_UINT32(0, g_dataIn->underruns());
    // The queue was not cleared under the RMT: the interrupt still finds its symbols.
    TEST_ASSERT_TRUE(fake_idf::play_all() > 0);
    fake_idf::state().failRmtDisable = false;
    g_dataIn->pump();  // disabled now: the queue is cleared, the pin goes back to UART2
    expectBackOnTheUart();
    TEST_ASSERT_EQUAL_UINT(1, fake_idf::state().rmtTransmits);
    startRun();  // the cancelled run left nothing behind: a new run starts from an empty queue
    TEST_ASSERT_EQUAL_UINT(2, fake_idf::state().rmtTransmits);
}

static void test_a_cancel_clears_the_queue_once_the_rmt_is_disabled(void)
{
    tracedAndConfigured();
    startRun();
    fake_idf::state().failRmtDisable = true;
    g_dataIn->stop();
    fake_idf::state().failRmtDisable = false;
    g_dataIn->pump();
    expectBackOnTheUart();
    TEST_ASSERT_EQUAL_UINT(0, fake_idf::play_all());  // nothing of the cancelled run is left
}

static void test_the_model_refuses_a_second_rmt_disable(void)
{
    rmt_channel_handle_t channel = nullptr;
    rmt_tx_channel_config_t config{};
    config.gpio_num = 21;
    TEST_ASSERT_EQUAL_INT(ESP_OK, rmt_new_tx_channel(&config, &channel));
    TEST_ASSERT_EQUAL_INT(ESP_OK, rmt_enable(channel));
    TEST_ASSERT_EQUAL_INT(ESP_OK, rmt_disable(channel));
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE, rmt_disable(channel));  // 259, as on the bench
    TEST_ASSERT_EQUAL_UINT(1, fake_idf::state().rmtDisableRefused);
    TEST_ASSERT_EQUAL_UINT(1, fake_idf::state().errorLogs);
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
    RUN_TEST(test_release_after_a_run_resets_the_pin_and_disables);
    RUN_TEST(test_release_mid_run_stops_it_first);
    RUN_TEST(test_release_without_configure_does_nothing);
    RUN_TEST(test_release_with_a_failed_reset_is_not_released);
    RUN_TEST(test_configure_after_a_failed_release_does_not_claim_the_line);
    RUN_TEST(test_recover_after_a_failed_release_keeps_the_fault);
    RUN_TEST(test_give_back_after_a_failed_release_is_lost);
    RUN_TEST(test_a_completed_release_then_configure_installs_uart2_again);
    RUN_TEST(test_trace_reports_each_hand_over_call_with_its_result);
    RUN_TEST(test_a_run_in_flight_keeps_the_rmt_without_logging);
    RUN_TEST(test_normal_end_comes_from_the_rmt_and_disables_once);
    RUN_TEST(test_cancel_disables_once_and_gives_the_pin_back);
    RUN_TEST(test_a_new_run_after_a_cancel_starts_and_ends);
    RUN_TEST(test_portions_after_the_rmt_ran_dry_cut_the_run);
    RUN_TEST(test_a_failed_disable_on_stop_keeps_the_rmt_and_completes_later);
    RUN_TEST(test_a_failed_delete_after_a_run_keeps_the_channel_and_completes_later);
    RUN_TEST(test_release_with_a_failed_delete_can_be_completed_later);
    RUN_TEST(test_a_failed_disable_keeps_a_busy_queue_until_the_rmt_is_disabled);
    RUN_TEST(test_a_cancel_clears_the_queue_once_the_rmt_is_disabled);
    RUN_TEST(test_the_model_refuses_a_second_rmt_disable);
    return UNITY_END();
}
