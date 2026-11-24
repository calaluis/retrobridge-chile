// EspSioUart (menu-sio-fisico T4) against the fake ESP-IDF: UART2 at 19200 on
// GPIO21/GPIO33, COMMAND as an input on GPIO39, waits bounded by their timeout,
// and every failure reported as TransportError.

#include <array>
#include <cstdint>
#include <vector>

#include "fake_idf.hpp"
#include "rb/esp/esp_sio_uart.hpp"
#include "unity.h"

using rb::core::ErrorCode;
using rb::core::Span;
using rb::esp::EspSioUart;

namespace {

constexpr EspSioUart::Pins kPins{39, 33, 21};

bool contains(const std::vector<int>& pins, int pin)
{
    for (int p : pins) {
        if (p == pin) {
            return true;
        }
    }
    return false;
}

}  // namespace

void setUp()
{
    fake_idf::reset();
    fake_idf::state().commandPin = 39;
}
void tearDown() {}

void test_begin_sets_uart2_at_19200_on_the_sio_pins()
{
    EspSioUart line{kPins};
    TEST_ASSERT_TRUE(line.begin());
    const auto& s = fake_idf::state();
    TEST_ASSERT_TRUE(s.uartInstalled);
    TEST_ASSERT_EQUAL_INT(19200, s.uartBaud);
    // Each byte reaches the driver at once: with the defaults a command frame arrived after kFrameReadUs.
    TEST_ASSERT_EQUAL_INT(1, s.uartRxFullThreshold);
    TEST_ASSERT_EQUAL_INT(1, s.uartRxTimeout);
    TEST_ASSERT_EQUAL_INT(21, s.uartTxPin);
    TEST_ASSERT_EQUAL_INT(33, s.uartRxPin);
    TEST_ASSERT_TRUE(contains(s.inputs, 39));
    TEST_ASSERT_TRUE(contains(s.pullups, 33));
    TEST_ASSERT_FALSE(contains(s.pullups, 39));  // COMMAND has R11 on the board
}

void test_a_failed_install_leaves_the_line_off()
{
    fake_idf::state().failUartInstall = true;
    EspSioUart line{kPins};
    TEST_ASSERT_FALSE(line.begin());
    TEST_ASSERT_FALSE(line.ready());
    std::array<uint8_t, 5> out{};
    TEST_ASSERT_FALSE(line.read(Span<uint8_t>{out}, 1000).ok());
    const uint8_t ack = 0x41;
    TEST_ASSERT_FALSE(line.write(Span<const uint8_t>{&ack, 1}).ok());
    TEST_ASSERT_TRUE(fake_idf::state().written.empty());
}

void test_wait_for_command_low_returns_when_it_falls()
{
    EspSioUart line{kPins};
    TEST_ASSERT_TRUE(line.begin());
    auto& s = fake_idf::state();
    s.commandLowFromUs = 5500;
    s.commandLowUntilUs = 9000;
    TEST_ASSERT_TRUE(line.waitCommandLevel(true, 20000));
    TEST_ASSERT_TRUE(s.nowUs >= 5500 && s.nowUs <= 6500);  // seen within one tick
    TEST_ASSERT_FALSE(line.commandHigh());
    TEST_ASSERT_TRUE(line.waitCommandLevel(false, 5000));
    TEST_ASSERT_TRUE(s.nowUs >= 9000 && s.nowUs <= 10000);
}

void test_wait_for_command_times_out_on_time()
{
    EspSioUart line{kPins};
    TEST_ASSERT_TRUE(line.begin());
    auto& s = fake_idf::state();
    TEST_ASSERT_FALSE(line.waitCommandLevel(true, 20000));
    TEST_ASSERT_TRUE(s.nowUs >= 20000 && s.nowUs < 20100);
    TEST_ASSERT_TRUE(s.taskDelays >= 19);  // the task yields while it waits
}

void test_read_returns_what_arrived_and_fails_on_a_driver_error()
{
    EspSioUart line{kPins};
    TEST_ASSERT_TRUE(line.begin());
    auto& s = fake_idf::state();
    s.rx = {0x31, 0x52, 0x01};
    std::array<uint8_t, 5> out{};
    const auto got = line.read(Span<uint8_t>{out}, 6000);
    TEST_ASSERT_TRUE(got.ok());
    TEST_ASSERT_EQUAL_UINT(3, got.value());
    TEST_ASSERT_EQUAL_UINT8(0x52, out[1]);
    s.failUartRead = true;
    const auto failed = line.read(Span<uint8_t>{out}, 6000);
    TEST_ASSERT_FALSE(failed.ok());
    TEST_ASSERT_EQUAL(ErrorCode::TransportError, failed.error());
}

void test_write_waits_for_the_last_bit_and_fails_on_a_driver_error()
{
    EspSioUart line{kPins};
    TEST_ASSERT_TRUE(line.begin());
    auto& s = fake_idf::state();
    const std::array<uint8_t, 2> bytes{0x41, 0x43};
    TEST_ASSERT_TRUE(line.write(Span<const uint8_t>{bytes}).ok());
    TEST_ASSERT_EQUAL_UINT(2, s.written.size());
    TEST_ASSERT_EQUAL_UINT(1, s.uartWaitTxDone);
    s.failUartWrite = true;
    TEST_ASSERT_FALSE(line.write(Span<const uint8_t>{bytes}).ok());
}

void test_flush_and_idle_reach_the_driver()
{
    EspSioUart line{kPins};
    TEST_ASSERT_TRUE(line.begin());
    auto& s = fake_idf::state();
    s.rx = {1, 2, 3};
    line.flushInput();
    TEST_ASSERT_TRUE(s.rx.empty());
    line.idleLine();
    TEST_ASSERT_EQUAL_UINT(1, s.uartWaitTxDone);
}

// The fake time as the session's clock.
class IdfClock final : public rb::app::Clock {
public:
    uint64_t nowMicros() const override { return static_cast<uint64_t>(fake_idf::state().nowUs); }
    void waitUntil(uint64_t micros) override
    {
        auto& s = fake_idf::state();
        if (static_cast<int64_t>(micros) > s.nowUs) {
            s.nowUs = static_cast<int64_t>(micros);
        }
    }
};

void test_a_whole_frame_through_the_transport()
{
    EspSioUart line{kPins};
    TEST_ASSERT_TRUE(line.begin());
    auto& s = fake_idf::state();
    s.rx = {0xEE};  // stale: arrived before the window
    // COMMAND falls at 3000; t0 later the 5 bytes, one byte time apart; t1 after the last, it rises.
    const uint8_t frame[5] = {0x31, 0x52, 0x01, 0x00, 0x84};
    int64_t at = 3000 + 900;
    for (uint8_t b : frame) {
        at += 521;
        s.rxAt.emplace_back(at, b);
    }
    s.commandLowFromUs = 3000;
    s.commandLowUntilUs = at + 800;
    IdfClock clock;
    rb::sio::SioLineTransport transport{line, clock};
    const auto received = transport.waitCommandFrame(20000);
    TEST_ASSERT_TRUE(received.ok());
    TEST_ASSERT_EQUAL_UINT8(5, received.value().count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame, received.value().bytes.data(), 5);
    TEST_ASSERT_TRUE(received.value().commandReleasedAtUs >= static_cast<uint64_t>(at + 800));
    TEST_ASSERT_TRUE(received.value().commandReleasedAtUs <= static_cast<uint64_t>(at + 1800));
    // The fall was seen up to a tick late, and still no frame byte was lost.
    TEST_ASSERT_TRUE(received.value().commandAssertedAtUs <= 4000);
}

void test_a_late_fall_still_keeps_the_first_bytes()
{
    EspSioUart line{kPins};
    TEST_ASSERT_TRUE(line.begin());
    auto& s = fake_idf::state();
    // The fall happens between two polls: the first byte (t0 = 750 us) is already in the UART.
    s.nowUs = 0;
    s.commandLowFromUs = 100;
    const uint8_t frame[5] = {0x31, 0x53, 0x00, 0x00, 0x84};
    int64_t at = 100 + 750;
    for (uint8_t b : frame) {
        s.rxAt.emplace_back(at, b);
        at += 521;
    }
    s.commandLowUntilUs = at + 700;
    IdfClock clock;
    rb::sio::SioLineTransport transport{line, clock};
    const auto received = transport.waitCommandFrame(20000);
    TEST_ASSERT_TRUE(received.ok());
    TEST_ASSERT_EQUAL_UINT8(5, received.value().count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(frame, received.value().bytes.data(), 5);
}

// menu-cinta decision 3 (R9): with the TX ring of the shared mode, write() ends at the same line time as
// without it (a 129-byte answer at 19200 included: one more than the FIFO). The model sends 10 bits a byte.
int64_t writeTime(int ring, std::size_t size)
{
    fake_idf::reset();
    fake_idf::state().commandPin = 39;
    fake_idf::state().txModel = true;
    EspSioUart line{kPins, ring};
    TEST_ASSERT_TRUE(line.begin());
    TEST_ASSERT_EQUAL_INT(ring, fake_idf::state().txRing);
    std::vector<uint8_t> bytes(size, 0x41);
    const int64_t start = fake_idf::state().nowUs;
    TEST_ASSERT_TRUE(line.write(Span<const uint8_t>{bytes.data(), bytes.size()}).ok());
    TEST_ASSERT_EQUAL_INT(0, fake_idf::tx_in_flight());  // the last bit is out when write() returns
    return fake_idf::state().nowUs - start;
}

void test_write_ends_at_the_same_line_time_with_the_tx_ring()
{
    for (const std::size_t size : {std::size_t{1}, std::size_t{4}, std::size_t{128}, std::size_t{129}}) {
        const int64_t without = writeTime(EspSioUart::kUartTxBuffer, size);
        const int64_t with = writeTime(EspSioUart::kSharedTxRing, size);
        TEST_ASSERT_EQUAL_INT64(without, with);
        const int64_t byteUs = (10 * 1000000 + 19199) / 19200;
        TEST_ASSERT_EQUAL_INT64(static_cast<int64_t>(size) * byteUs, with);
    }
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_begin_sets_uart2_at_19200_on_the_sio_pins);
    RUN_TEST(test_a_failed_install_leaves_the_line_off);
    RUN_TEST(test_wait_for_command_low_returns_when_it_falls);
    RUN_TEST(test_wait_for_command_times_out_on_time);
    RUN_TEST(test_read_returns_what_arrived_and_fails_on_a_driver_error);
    RUN_TEST(test_write_waits_for_the_last_bit_and_fails_on_a_driver_error);
    RUN_TEST(test_flush_and_idle_reach_the_driver);
    RUN_TEST(test_a_whole_frame_through_the_transport);
    RUN_TEST(test_a_late_fall_still_keeps_the_first_bytes);
    RUN_TEST(test_write_ends_at_the_same_line_time_with_the_tx_ring);
    return UNITY_END();
}
