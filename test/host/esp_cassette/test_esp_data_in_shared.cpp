// rb_esp_cassette: EspDataIn in shared mode (menu-cinta T9a.2, plan §2.4, decision 3 and risks R9, R11).
// UART2 is the disk's, installed once at boot; the cassette uses it without installing or deleting it.
// Runs the real driver against fake_idf with its TX model on: TX ring with per-write headers, the
// 128-byte FIFO, the shift register and the time per byte. What it proves: send() never blocks, never
// more than 32 bytes are on their way, the line comes back within kReleaseTimeoutUs, and the cassette
// confirms only with the hardware free. What it cannot prove: that the real driver and HAL behave as
// the model (the bench of T9b does).

#include <cstdint>
#include <new>
#include <vector>

#include "driver/uart.h"
#include "fakes/idf/fake_idf.hpp"
#include "rb/board/board_profile.hpp"
#include "rb/board/fujinet_atari_v1x.hpp"
#include "rb/esp/esp_data_in.hpp"
#include "rb/menu/data_in_arbiter.hpp"
#include "unity.h"

using fake_idf::Owner;
using rb::board::BoardProfile;
using rb::cassette::FskPortion;
using rb::esp::EspDataIn;
using GiveBack = rb::esp::EspDataIn::GiveBack;

namespace {

constexpr int64_t kTickUs = 2000;  // the cassette task's tick
constexpr int kDataIn = 21;
constexpr int kDataOut = 33;

rb::board::CassetteOutputAssignment granted()
{
    BoardProfile p = rb::board::fujinet_atari_v1x_profile();
    p.sioGateApproved = true;
    p.motorGateApproved = true;
    return rb::board::validate_for_cassette_output(p).value();
}

alignas(EspDataIn) unsigned char g_storage[sizeof(EspDataIn)];
EspDataIn* g_dataIn = nullptr;

// The disk installed UART2 at boot (with `ring` bytes of TX ring) and routed DATA IN to its TX; then the
// cassette attaches in shared mode at 600 baud. The install and pin counters start from zero here.
void shared(int ring)
{
    fake_idf::reset();
    auto& s = fake_idf::state();
    s.txModel = true;
    s.uartBaud = 19200;
    TEST_ASSERT_EQUAL(ESP_OK, uart_driver_install(UART_NUM_2, 1024, ring, 0, nullptr, 0));
    TEST_ASSERT_EQUAL(ESP_OK, uart_set_pin(UART_NUM_2, kDataIn, kDataOut, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    s.uartInstalls = 0;
    s.uartDeletes = 0;
    s.uartSetPinCalls = 0;
    g_dataIn = new (g_storage) EspDataIn();
    TEST_ASSERT_TRUE(g_dataIn->attachShared(granted(), static_cast<std::size_t>(ring)));
    TEST_ASSERT_TRUE(g_dataIn->shared());
    TEST_ASSERT_TRUE(g_dataIn->setBaud(600));
}

// A record source as the player behaves: wants `count` bytes now; refused, it asks for 1 byte next tick.
struct Source {
    int left = 0;
    int want = 0;
    std::vector<uint8_t> bytes = std::vector<uint8_t>(512, 0x55);

    void tick()
    {
        if (left == 0) {
            return;
        }
        const int n = want < left ? want : left;
        if (g_dataIn->send(rb::core::Span<const uint8_t>(bytes.data(), static_cast<std::size_t>(n)))) {
            left -= n;
            want = 512;  // the clock owes more; the player asks for what it owes, up to kMaxSend
        } else {
            want = 1;  // refused: the player keeps at most one byte of owed time
        }
    }
};

// Plays `ticks` cassette ticks: the source sends, then the line runs for one tick.
void play(Source& src, int ticks)
{
    for (int i = 0; i < ticks; ++i) {
        src.tick();
        fake_idf::tx_advance(kTickUs);
    }
}

void startRun()
{
    TEST_ASSERT_TRUE(g_dataIn->push(FskPortion{false, 200}));
    TEST_ASSERT_TRUE(g_dataIn->push(FskPortion{true, 200}));
    g_dataIn->pump();
    g_dataIn->pump();
    TEST_ASSERT_TRUE(g_dataIn->fskActive());
}

// Polls giveBack() every tick (the player already discards); returns the line time it took to get Uart.
int64_t giveBackTime()
{
    const int64_t start = fake_idf::state().nowUs;
    for (int i = 0; i < 2000; ++i) {
        if (g_dataIn->giveBack() == GiveBack::Uart) {
            return fake_idf::state().nowUs - start;
        }
        fake_idf::tx_advance(kTickUs);
    }
    TEST_FAIL_MESSAGE("the line never came back");
    return -1;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// ---- R11: no driver install or delete, ever, in shared mode ----

void test_shared_mode_never_installs_nor_deletes_uart2()
{
    shared(2048);
    Source src{40, 512};
    play(src, 400);
    startRun();
    fake_idf::play_all();
    g_dataIn->pump();
    TEST_ASSERT_FALSE(g_dataIn->fskActive());
    TEST_ASSERT_EQUAL(GiveBack::Uart, g_dataIn->giveBack());
    TEST_ASSERT_TRUE(g_dataIn->release());
    TEST_ASSERT_EQUAL_UINT(0, fake_idf::state().uartInstalls);
    TEST_ASSERT_EQUAL_UINT(0, fake_idf::state().uartDeletes);
    TEST_ASSERT_TRUE(fake_idf::state().uartInstalled);       // still the disk's
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Uart);  // and DATA IN still its TX
}

// The RMT allocates once per FSK run (documented): one channel for one run, in this task.
void test_one_rmt_channel_per_run()
{
    shared(2048);
    startRun();
    fake_idf::play_all();
    g_dataIn->pump();
    startRun();
    fake_idf::play_all();
    g_dataIn->pump();
    TEST_ASSERT_EQUAL_UINT(2, fake_idf::state().rmtNewChannels);
    TEST_ASSERT_EQUAL_UINT(2, fake_idf::state().rmtDelChannelCalls);
}

// ---- R9: send() never blocks and keeps at most 32 bytes on their way ----

void test_send_never_blocks_and_keeps_at_most_32_bytes_in_flight()
{
    shared(2048);
    Source src{512, 512};
    play(src, 6000);  // 12 s: 512 bytes at 600 baud take ~8.5 s
    TEST_ASSERT_EQUAL_INT(0, src.left);
    TEST_ASSERT_EQUAL_INT64(0, fake_idf::state().txWriteBlockedUs);
    TEST_ASSERT_LESS_OR_EQUAL_INT(32, fake_idf::state().txMaxInFlight);
    TEST_ASSERT_EQUAL_UINT(512, fake_idf::state().written.size());
}

// The interrupt moves bytes from the ring to the FIFO between the two reads: counted twice, not zero.
void test_bytes_moved_between_the_two_reads_never_count_as_gone()
{
    shared(2048);
    auto& s = fake_idf::state();
    s.txIsrHeld = true;            // the interrupt runs only between the ring read and the FIFO read
    s.txIsrAfterRingRead = true;
    Source src{512, 512};
    play(src, 1000);
    TEST_ASSERT_EQUAL_INT64(0, s.txWriteBlockedUs);
    TEST_ASSERT_LESS_OR_EQUAL_INT(32, s.txMaxInFlight);
}

// An empty ring says nothing about the FIFO: a full FIFO refuses even one byte.
void test_an_empty_ring_with_a_full_fifo_refuses()
{
    shared(2048);
    fake_idf::state().txFifo = 128;
    const uint8_t one = 0x55;
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(129, g_dataIn->inFlight());
    TEST_ASSERT_FALSE(g_dataIn->send(rb::core::Span<const uint8_t>(&one, 1)));
    TEST_ASSERT_EQUAL_UINT(0, fake_idf::state().written.size());
}

// A refused send() takes nothing (ByteOut's contract): the player keeps the bytes for a later tick.
void test_a_send_that_does_not_fit_takes_nothing()
{
    shared(2048);
    std::vector<uint8_t> bytes(512, 0x55);
    TEST_ASSERT_FALSE(g_dataIn->send(rb::core::Span<const uint8_t>(bytes.data(), 512)));
    TEST_ASSERT_EQUAL_UINT(0, fake_idf::state().written.size());
    TEST_ASSERT_TRUE(g_dataIn->send(rb::core::Span<const uint8_t>(bytes.data(), 1)));
}

// With 512 bytes owed, the line comes back within kReleaseTimeoutUs (600 ms).
void test_the_line_comes_back_within_the_release_timeout()
{
    shared(2048);
    Source src{512, 512};
    play(src, 300);  // mid-record, as much in flight as the rule allows
    TEST_ASSERT_GREATER_THAN_UINT(1, g_dataIn->inFlight());
    TEST_ASSERT_LESS_OR_EQUAL_INT64(static_cast<int64_t>(rb::host::DataInArbiter::kReleaseTimeoutUs), giveBackTime());
}

// ---- the alternative without a TX ring: the same bound ----

void test_without_a_ring_send_writes_only_on_an_empty_line_and_never_blocks()
{
    shared(0);
    std::vector<uint8_t> bytes(64, 0x55);
    TEST_ASSERT_FALSE(g_dataIn->send(rb::core::Span<const uint8_t>(bytes.data(), 33)));  // over 32
    TEST_ASSERT_TRUE(g_dataIn->send(rb::core::Span<const uint8_t>(bytes.data(), 32)));
    TEST_ASSERT_FALSE(g_dataIn->send(rb::core::Span<const uint8_t>(bytes.data(), 1)));   // still going out
    Source src{512, 512};
    play(src, 6000);
    TEST_ASSERT_EQUAL_INT(0, src.left);
    TEST_ASSERT_EQUAL_INT64(0, fake_idf::state().txWriteBlockedUs);
    TEST_ASSERT_LESS_OR_EQUAL_INT(32, fake_idf::state().txMaxInFlight);
}

void test_without_a_ring_the_line_comes_back_within_the_release_timeout()
{
    shared(0);
    Source src{512, 512};
    play(src, 300);
    TEST_ASSERT_LESS_OR_EQUAL_INT64(static_cast<int64_t>(rb::host::DataInArbiter::kReleaseTimeoutUs), giveBackTime());
}

// ---- R11: "hardware libre" ----

void test_hardware_free_only_when_everything_is_idle()
{
    shared(2048);
    TEST_ASSERT_TRUE(g_dataIn->hardwareFree());

    const uint8_t one = 0x55;
    TEST_ASSERT_TRUE(g_dataIn->send(rb::core::Span<const uint8_t>(&one, 1)));
    TEST_ASSERT_FALSE(g_dataIn->hardwareFree());  // a byte still going out
    fake_idf::tx_advance(20000);
    TEST_ASSERT_TRUE(g_dataIn->hardwareFree());

    startRun();
    TEST_ASSERT_FALSE(g_dataIn->hardwareFree());  // an FSK run active
    fake_idf::play_all();
    g_dataIn->pump();
    TEST_ASSERT_TRUE(g_dataIn->hardwareFree());
}

void test_hardware_not_free_while_the_rmt_keeps_the_pin()
{
    shared(2048);
    startRun();
    fake_idf::state().failRmtDisable = true;
    g_dataIn->stop();
    TEST_ASSERT_TRUE(g_dataIn->releasePending());
    TEST_ASSERT_FALSE(g_dataIn->hardwareFree());
    TEST_ASSERT_EQUAL(GiveBack::Busy, g_dataIn->giveBack());
    fake_idf::state().failRmtDisable = false;
    TEST_ASSERT_EQUAL(GiveBack::Uart, g_dataIn->giveBack());
    TEST_ASSERT_TRUE(g_dataIn->hardwareFree());
}

void test_hardware_not_free_while_faulted()
{
    shared(2048);
    fake_idf::state().failUartSetPin = true;
    TEST_ASSERT_EQUAL(GiveBack::Lost, g_dataIn->giveBack());
    TEST_ASSERT_TRUE(g_dataIn->faulted());
    TEST_ASSERT_FALSE(g_dataIn->hardwareFree());
}

void test_hardware_not_free_with_portions_queued()
{
    shared(2048);
    TEST_ASSERT_TRUE(g_dataIn->push(FskPortion{false, 200}));
    TEST_ASSERT_FALSE(g_dataIn->hardwareFree());  // a run would start on the next pump()
}

// ---- giveBack(): the Fault recovery checks the GPIO matrix ----

void test_give_back_is_busy_while_bytes_go_out()
{
    shared(2048);
    const uint8_t bytes[8] = {};
    TEST_ASSERT_TRUE(g_dataIn->send(rb::core::Span<const uint8_t>(bytes, 8)));
    TEST_ASSERT_EQUAL(GiveBack::Busy, g_dataIn->giveBack());
    fake_idf::tx_advance(200000);
    TEST_ASSERT_EQUAL(GiveBack::Uart, g_dataIn->giveBack());
}

void test_give_back_routes_the_pin_to_uart2_and_checks_the_matrix()
{
    shared(2048);
    startRun();
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Rmt);
    TEST_ASSERT_EQUAL(GiveBack::Uart, g_dataIn->giveBack());  // ends the run, pin back to UART2
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Uart);
    TEST_ASSERT_EQUAL_UINT32(fake_idf::kU2TxdOutIdx, fake_idf::state().outSel);
    TEST_ASSERT_FALSE(g_dataIn->fskActive());
}

void test_a_failed_uart_set_pin_loses_the_line_safely()
{
    shared(2048);
    fake_idf::state().failUartSetPin = true;
    TEST_ASSERT_EQUAL(GiveBack::Lost, g_dataIn->giveBack());
    TEST_ASSERT_TRUE(fake_idf::state().owner == Owner::Gpio);  // reset: output off, the 74LS07 lets go
    TEST_ASSERT_TRUE(g_dataIn->faulted());
    const unsigned errors = fake_idf::state().errorLogs;
    TEST_ASSERT_EQUAL(GiveBack::Lost, g_dataIn->giveBack());  // still failing: no new error line
    TEST_ASSERT_EQUAL_UINT(errors, fake_idf::state().errorLogs);
    fake_idf::state().failUartSetPin = false;
    TEST_ASSERT_EQUAL(GiveBack::Uart, g_dataIn->giveBack());  // recovered
    TEST_ASSERT_FALSE(g_dataIn->faulted());
}

void test_a_matrix_that_reads_otherwise_loses_the_line()
{
    shared(2048);
    auto& s = fake_idf::state();
    s.routeForced = true;
    s.forcedSel = fake_idf::kRmtOut0Idx;  // uart_set_pin said OK, the matrix says the RMT
    s.forcedEnabled = true;
    TEST_ASSERT_EQUAL(GiveBack::Lost, g_dataIn->giveBack());
    TEST_ASSERT_TRUE(g_dataIn->faulted());
    s.forcedSel = fake_idf::kU2TxdOutIdx;
    s.forcedEnabled = false;  // the right signal, output off: still not driven by UART2
    TEST_ASSERT_EQUAL(GiveBack::Lost, g_dataIn->giveBack());
    s.routeForced = false;
    TEST_ASSERT_EQUAL(GiveBack::Uart, g_dataIn->giveBack());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_shared_mode_never_installs_nor_deletes_uart2);
    RUN_TEST(test_one_rmt_channel_per_run);
    RUN_TEST(test_send_never_blocks_and_keeps_at_most_32_bytes_in_flight);
    RUN_TEST(test_bytes_moved_between_the_two_reads_never_count_as_gone);
    RUN_TEST(test_an_empty_ring_with_a_full_fifo_refuses);
    RUN_TEST(test_a_send_that_does_not_fit_takes_nothing);
    RUN_TEST(test_the_line_comes_back_within_the_release_timeout);
    RUN_TEST(test_without_a_ring_send_writes_only_on_an_empty_line_and_never_blocks);
    RUN_TEST(test_without_a_ring_the_line_comes_back_within_the_release_timeout);
    RUN_TEST(test_hardware_free_only_when_everything_is_idle);
    RUN_TEST(test_hardware_not_free_while_the_rmt_keeps_the_pin);
    RUN_TEST(test_hardware_not_free_while_faulted);
    RUN_TEST(test_hardware_not_free_with_portions_queued);
    RUN_TEST(test_give_back_is_busy_while_bytes_go_out);
    RUN_TEST(test_give_back_routes_the_pin_to_uart2_and_checks_the_matrix);
    RUN_TEST(test_a_failed_uart_set_pin_loses_the_line_safely);
    RUN_TEST(test_a_matrix_that_reads_otherwise_loses_the_line);
    return UNITY_END();
}
