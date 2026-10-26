// Integration of the H5 host components: CommandFrameAssembler -> SioCommandSource
// -> SioListener (docs/diseno/h5-escucha-solo-recepcion.md section 9).
//
// AssemblingCommandSource below is a TEST-ONLY adapter: it plays the role the
// future EspSioTransport will have, replaying a script of timed events instead
// of a COMMAND ISR and a UART. It is not production code and fixes no physical
// capture mechanism (D1): the script says explicitly when complete() is called.
// These tests say nothing about real latency or the electrical side of the bus
// (H4 M3-M5).
//
// Mapping under test (the adapter's job):
//   Frame        -> CommandBytes
//   ReceiveError -> TransportError (UART errors only, D2)
//   Dropped      -> keep waiting; the reason stays in AssemblerStats (D2, D4)
//   deadline     -> Timeout, keeping any open window for the next call
// The adapter calls poll() at every event and at the deadline (precondition).

#include <array>
#include <cstddef>
#include <cstdint>

#include "fakes/fake_clock.hpp"
#include "rb/app/ports/sio_command_source.hpp"
#include "rb/core/assert.hpp"
#include "rb/core/span.hpp"
#include "rb/sio/command_frame_assembler.hpp"
#include "rb/sio/sio_checksum.hpp"
#include "rb/sio/sio_listener.hpp"
#include "unity.h"

using rb::app::CommandBytes;
using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;
using rb::core::Span;
using rb::sio::AssemblyOutcome;
using rb::sio::AssemblyResult;
using rb::sio::CommandFrameAssembler;
using rb::sio::CommandLineLimits;
using rb::sio::ListenOutcome;
using rb::sio::SioListener;
using rb::test::FakeClock;

namespace {

constexpr CommandLineLimits kLimits{100, 6000, 50000};  // test values, not real thresholds (P6)
constexpr uint32_t kWaitUs = 20000;                     // test value

class AssemblingCommandSource final : public rb::app::SioCommandSource {
public:
    enum class Kind : uint8_t { Fall, Rise, Bytes, ReceiveError, QueueOverflow, Complete };

    AssemblingCommandSource(FakeClock& clock, CommandFrameAssembler& assembler) : clock_(clock), assembler_(assembler) {}

    void fall(uint64_t atUs) { push({atUs, Kind::Fall, {}, 0}); }
    void rise(uint64_t atUs) { push({atUs, Kind::Rise, {}, 0}); }
    void receive_error(uint64_t atUs) { push({atUs, Kind::ReceiveError, {}, 0}); }
    void queue_overflow(uint64_t atUs) { push({atUs, Kind::QueueOverflow, {}, 0}); }
    void complete(uint64_t atUs) { push({atUs, Kind::Complete, {}, 0}); }
    void bytes(uint64_t atUs, const uint8_t* data, std::size_t length)
    {
        Event event{atUs, Kind::Bytes, {}, static_cast<uint8_t>(length)};
        RB_ASSERT(length <= event.data.size());
        for (std::size_t i = 0; i < length; ++i) {
            event.data[i] = data[i];
        }
        push(event);
    }

    unsigned completions() const { return completions_; }

    Result<CommandBytes> waitCommandFrame(uint32_t timeoutUs) override
    {
        const uint64_t deadline = clock_.nowMicros() + timeoutUs;
        while (next_ < size_ && events_[next_].atUs <= deadline) {
            const Event event = events_[next_++];
            clock_.waitUntil(event.atUs);
            assembler_.poll(event.atUs);
            switch (event.kind) {
            case Kind::Fall:
                assembler_.onFall(event.atUs);
                break;
            case Kind::Rise:
                assembler_.onRise(event.atUs);
                break;
            case Kind::Bytes:
                assembler_.onBytes(Span<const uint8_t>{event.data.data(), event.length});
                break;
            case Kind::ReceiveError:
                assembler_.onReceiveError();
                break;
            case Kind::QueueOverflow:
                assembler_.onQueueOverflow();
                break;
            case Kind::Complete:
            default: {
                ++completions_;
                const AssemblyResult result = assembler_.complete();
                if (result.outcome == AssemblyOutcome::Frame) {
                    return result.frame;
                }
                if (result.outcome == AssemblyOutcome::ReceiveError) {
                    return Failure{ErrorCode::TransportError};
                }
                break;  // Dropped or NothingToDeliver: keep waiting
            }
            }
        }
        clock_.waitUntil(deadline);
        assembler_.poll(deadline);
        return Failure{ErrorCode::Timeout};  // any open window is kept for the next call
    }

private:
    struct Event {
        uint64_t atUs;
        Kind kind;
        std::array<uint8_t, 8> data;
        uint8_t length;
    };

    void push(const Event& event)
    {
        RB_ASSERT(size_ < events_.size());
        RB_ASSERT(size_ == 0 || event.atUs >= events_[size_ - 1].atUs);  // scripts are in time order
        events_[size_++] = event;
    }

    FakeClock& clock_;
    CommandFrameAssembler& assembler_;
    std::array<Event, 48> events_{};
    std::size_t size_ = 0;
    std::size_t next_ = 0;
    unsigned completions_ = 0;
};

std::array<uint8_t, 5> valid_frame(uint8_t aux1)
{
    std::array<uint8_t, 5> bytes{0x31, 0x52, aux1, 0, 0};
    bytes[4] = rb::sio::sio_checksum(Span<const uint8_t>{bytes.data(), 4});
    return bytes;
}

CommandFrameAssembler make_assembler()
{
    auto created = CommandFrameAssembler::create(kLimits);
    TEST_ASSERT_TRUE(created.ok());
    return created.value();
}

struct Chain {
    FakeClock clock;
    CommandFrameAssembler assembler = make_assembler();
    AssemblingCommandSource source{clock, assembler};
    SioListener listener{source, clock};

    // A whole window: fall, the given bytes, rise, then complete() after the capture delay.
    void window(uint64_t fallUs, const uint8_t* data, std::size_t length, uint64_t lowUs, uint64_t captureUs = 600)
    {
        source.fall(fallUs);
        source.bytes(fallUs + lowUs / 2, data, length);
        source.rise(fallUs + lowUs);
        source.complete(fallUs + lowUs + captureUs);
    }
};

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_consecutive_valid_windows_reach_the_listener_once_each(void)
{
    Chain c;
    const auto a = valid_frame(1);
    const auto b = valid_frame(2);
    c.window(1000, a.data(), 5, 4000);
    c.window(20000, b.data(), 5, 4000);

    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::Frame);
    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::Frame);
    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::Timeout);  // nothing delivered twice
    TEST_ASSERT_EQUAL_UINT(2, c.listener.size());
    TEST_ASSERT_EQUAL_UINT64(5000, c.listener.at(0).atUs);   // COMMAND release of window A
    TEST_ASSERT_EQUAL_UINT64(24000, c.listener.at(1).atUs);  // ... and of window B
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b.data(), c.listener.at(1).bytes.data(), 5);
    TEST_ASSERT_EQUAL_UINT32(2, c.assembler.stats().frames);
    TEST_ASSERT_EQUAL_UINT32(2, c.listener.stats().frames);
}

static void test_invalid_windows_never_reach_the_listener_and_keep_their_reason(void)
{
    Chain c;
    const auto bytes = valid_frame(3);  // valid checksum, invalid windows (D4)
    c.window(1000, bytes.data(), 5, 50);     // glitch
    c.window(20000, bytes.data(), 5, 7000);  // overlong window

    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::Timeout);
    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::Timeout);
    TEST_ASSERT_EQUAL_UINT(0, c.listener.size());
    TEST_ASSERT_EQUAL_UINT32(0, c.listener.stats().transportErrors);  // never turned into a UART error
    TEST_ASSERT_EQUAL_UINT32(1, c.assembler.stats().glitches);       // the reasons are not lost
    TEST_ASSERT_EQUAL_UINT32(1, c.assembler.stats().overlongWindows);
    TEST_ASSERT_EQUAL_UINT32(0, c.assembler.stats().frames);
}

static void test_uart_error_reaches_the_listener_as_a_transport_error(void)
{
    Chain c;
    const auto bytes = valid_frame(4);
    c.source.fall(1000);
    c.source.bytes(2000, bytes.data(), 5);
    c.source.receive_error(2500);
    c.source.rise(5000);
    c.source.complete(5600);

    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::TransportError);
    TEST_ASSERT_EQUAL_UINT32(1, c.listener.stats().transportErrors);
    TEST_ASSERT_EQUAL_UINT32(1, c.assembler.stats().receiveErrors);
    TEST_ASSERT_EQUAL_UINT64(5600, c.listener.at(0).atUs);  // clock time of the failed wait
}

static void test_timeout_keeps_an_open_window_for_the_next_wait(void)
{
    Chain c;
    const auto bytes = valid_frame(5);
    c.source.fall(15000);
    c.source.bytes(16000, bytes.data(), 3);
    // the first wait ends at 20000 with COMMAND still low
    c.source.bytes(20500, bytes.data() + 3, 2);
    c.source.rise(21000);
    c.source.complete(21600);

    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::Timeout);
    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::Frame);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), c.listener.at(0).bytes.data(), 5);
    TEST_ASSERT_EQUAL_UINT64(21000, c.listener.at(0).atUs);
    TEST_ASSERT_EQUAL_UINT32(1, c.listener.stats().timeouts);
}

static void test_superseded_window_with_late_bytes_is_never_delivered(void)
{
    Chain c;
    const auto a = valid_frame(0xA0);
    const auto b = valid_frame(0xB0);
    c.source.fall(1000);
    c.source.bytes(2000, a.data(), 3);
    c.source.rise(5000);
    c.source.bytes(5300, a.data() + 3, 2);  // late bytes of A, and no complete() for A
    c.window(9000, b.data(), 5, 4000);

    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::Frame);
    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::Timeout);
    TEST_ASSERT_EQUAL_UINT(1, c.listener.size());
    TEST_ASSERT_EQUAL_UINT8(5, c.listener.at(0).count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b.data(), c.listener.at(0).bytes.data(), 5);
    TEST_ASSERT_EQUAL_UINT32(1, c.assembler.stats().superseded);
}

static void test_stuck_command_is_seen_by_the_assembler_and_the_bus_recovers(void)
{
    Chain c;
    const auto bytes = valid_frame(6);
    c.source.fall(1000);
    c.source.bytes(2000, bytes.data(), 5);
    // COMMAND stays low: the waits end on their deadlines and poll() reports StuckLow
    c.source.rise(90000);
    c.source.complete(90600);
    c.window(105000, bytes.data(), 5, 4000);  // normal operation afterwards

    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::Timeout);  // 20, 40, 60, 80 ms
    }
    TEST_ASSERT_EQUAL_UINT32(1, c.assembler.stats().stuckLows);  // reported once, by poll()
    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::Timeout);  // the stuck window is dropped
    TEST_ASSERT_EQUAL_UINT32(1, c.assembler.stats().recoveries);
    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::Frame);  // recovered
    TEST_ASSERT_EQUAL_UINT(1, c.listener.size());
    TEST_ASSERT_EQUAL_UINT32(0, c.listener.stats().transportErrors);
}

static void test_byte_level_problems_are_classified_by_the_listener(void)
{
    Chain c;
    auto bad = valid_frame(7);
    bad[4] ^= 0xFF;
    const auto good = valid_frame(8);
    const std::array<uint8_t, 7> seven{good[0], good[1], good[2], good[3], good[4], 0xAA, 0xBB};
    c.window(1000, bad.data(), 5, 4000);
    c.window(10000, good.data(), 3, 4000);
    c.window(20000, seven.data(), 7, 5000);

    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::ChecksumMismatch);
    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::Incomplete);
    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::Overlong);
    TEST_ASSERT_EQUAL_UINT8(7, c.listener.at(2).count);
    TEST_ASSERT_EQUAL_UINT32(3, c.assembler.stats().frames);  // valid windows, whatever their bytes
}

static void test_repeated_errors_then_recovery(void)
{
    Chain c;
    const auto bytes = valid_frame(9);
    for (uint64_t start = 1000; start <= 10000; start += 9000) {  // two windows with UART errors
        c.source.fall(start);
        c.source.receive_error(start + 500);
        c.source.rise(start + 4000);
        c.source.complete(start + 4600);
    }
    c.source.fall(20000);  // a window that loses events
    c.source.queue_overflow(20500);
    c.source.rise(24000);
    c.source.complete(24600);
    c.window(40000, bytes.data(), 5, 4000);  // then a good one

    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::TransportError);
    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::TransportError);
    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::Timeout);  // overflow window dropped
    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::Frame);
    TEST_ASSERT_EQUAL_UINT32(2, c.listener.stats().transportErrors);
    TEST_ASSERT_EQUAL_UINT32(1, c.assembler.stats().queueOverflows);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), c.listener.at(2).bytes.data(), 5);
}

static void test_a_second_complete_delivers_nothing(void)
{
    Chain c;
    const auto bytes = valid_frame(10);
    c.window(1000, bytes.data(), 5, 4000);
    c.source.complete(6000);  // the adapter calls complete() again

    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::Frame);
    TEST_ASSERT_TRUE(c.listener.step(kWaitUs) == ListenOutcome::Timeout);
    TEST_ASSERT_EQUAL_UINT(2, c.source.completions());
    TEST_ASSERT_EQUAL_UINT(1, c.listener.size());
    TEST_ASSERT_EQUAL_UINT32(1, c.assembler.stats().frames);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_consecutive_valid_windows_reach_the_listener_once_each);
    RUN_TEST(test_invalid_windows_never_reach_the_listener_and_keep_their_reason);
    RUN_TEST(test_uart_error_reaches_the_listener_as_a_transport_error);
    RUN_TEST(test_timeout_keeps_an_open_window_for_the_next_wait);
    RUN_TEST(test_superseded_window_with_late_bytes_is_never_delivered);
    RUN_TEST(test_stuck_command_is_seen_by_the_assembler_and_the_bus_recovers);
    RUN_TEST(test_byte_level_problems_are_classified_by_the_listener);
    RUN_TEST(test_repeated_errors_then_recovery);
    RUN_TEST(test_a_second_complete_delivers_nothing);
    return UNITY_END();
}
