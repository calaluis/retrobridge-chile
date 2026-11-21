// SioLineTransport (menu-sio-fisico T3, spec criterion 6): the SioTransport over
// the physical line. DATA IN is released after every reply and every error, a
// byte outside the COMMAND window never reaches a frame, and the COMMAND ->
// reply latency is recorded for the console.

#include <array>
#include <cstdint>
#include <cstring>

#include "fakes/fake_clock.hpp"
#include "rb/sio/sio_line_transport.hpp"
#include "unity.h"

using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;
using rb::core::Span;
using rb::sio::SioLineTransport;

namespace {

// A line driven by the test: each wait and read takes its next scripted answer.
class ScriptedLine final : public rb::sio::SioUartPort {
public:
    explicit ScriptedLine(rb::test::FakeClock& clock) : clock_(clock) {}

    // --- Script ---------------------------------------------------------------
    bool fallsIn = true;              // COMMAND goes low within the wait
    uint64_t fallAfterUs = 100;
    std::array<uint8_t, 300> pending{};  // what arrives on DATA OUT after the fall
    std::size_t pendingSize = 0;
    std::size_t strayBeforeFall = 0;  // bytes received before COMMAND fell (flushInput drops them)
    bool readFails = false;
    bool risesIn = true;              // COMMAND goes back high after the frame
    uint64_t riseAfterUs = 800;
    bool writeFails = false;
    uint64_t writeStartDelayUs = 0;   // the reply waits this long after the rise

    // --- Observations ---------------------------------------------------------
    unsigned idles = 0;
    unsigned flushes = 0;
    unsigned writes = 0;
    std::array<uint8_t, 1100> written{};
    std::size_t writtenSize = 0;
    uint32_t lastReadTimeoutUs = 0;

    void arrive(const uint8_t* bytes, std::size_t n)
    {
        std::memcpy(pending.data() + pendingSize, bytes, n);
        pendingSize += n;
    }

    bool waitCommandLevel(bool asserted, uint32_t timeoutUs) override
    {
        if (asserted) {
            if (!fallsIn) {
                clock_.advance(timeoutUs);
                return false;
            }
            clock_.advance(fallAfterUs);
            return true;
        }
        if (!risesIn) {
            clock_.advance(timeoutUs);
            return false;
        }
        clock_.advance(riseAfterUs);
        return true;
    }

    Result<std::size_t> read(Span<uint8_t> out, uint32_t timeoutUs) override
    {
        lastReadTimeoutUs = timeoutUs;
        if (readFails) {
            return Failure{ErrorCode::TransportError};
        }
        // Stray bytes were dropped by flushInput() only if it was called.
        std::size_t from = flushes > 0 ? strayBeforeFall : 0;
        std::size_t n = 0;
        while (n < out.size() && from + n < pendingSize) {
            out[n] = pending[from + n];
            ++n;
        }
        std::memmove(pending.data(), pending.data() + from + n, pendingSize - from - n);
        pendingSize -= from + n;
        strayBeforeFall = 0;
        clock_.advance(SioLineTransport::byteTimeUs(n));
        return n;
    }

    Result<void> write(Span<const uint8_t> bytes) override
    {
        clock_.advance(writeStartDelayUs);
        writeStartDelayUs = 0;
        ++writes;
        if (writeFails) {
            return Failure{ErrorCode::TransportError};
        }
        std::memcpy(written.data() + writtenSize, bytes.data(), bytes.size());
        writtenSize += bytes.size();
        clock_.advance(SioLineTransport::byteTimeUs(bytes.size()));
        return {};
    }

    void flushInput() override { ++flushes; }
    void idleLine() override { ++idles; }

private:
    rb::test::FakeClock& clock_;
};

constexpr std::array<uint8_t, 5> kRead1{0x31, 0x52, 0x01, 0x00, 0x84};

struct Rig {
    rb::test::FakeClock clock;
    ScriptedLine line{clock};
    SioLineTransport transport{line, clock};
};

}  // namespace

void setUp() {}
void tearDown() {}

void test_no_command_is_a_timeout_and_touches_nothing()
{
    Rig rig;
    rig.line.fallsIn = false;
    const auto received = rig.transport.waitCommandFrame(20000);
    TEST_ASSERT_FALSE(received.ok());
    TEST_ASSERT_EQUAL(ErrorCode::Timeout, received.error());
    TEST_ASSERT_EQUAL_UINT(0, rig.line.writes);
}

void test_a_frame_in_the_window_is_returned_with_both_edges()
{
    Rig rig;
    rig.clock.advance(1000);
    rig.line.arrive(kRead1.data(), kRead1.size());
    const auto received = rig.transport.waitCommandFrame(20000);
    TEST_ASSERT_TRUE(received.ok());
    TEST_ASSERT_EQUAL_UINT8(5, received.value().count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kRead1.data(), received.value().bytes.data(), 5);
    TEST_ASSERT_EQUAL_UINT64(1100, received.value().commandAssertedAtUs);
    TEST_ASSERT_EQUAL_UINT64(1100 + SioLineTransport::byteTimeUs(5) + 800, received.value().commandReleasedAtUs);
    TEST_ASSERT_EQUAL_UINT32(SioLineTransport::kFrameReadUs, rig.line.lastReadTimeoutUs);
    TEST_ASSERT_EQUAL_UINT32(1, rig.transport.stats().frames);
}

void test_bytes_before_the_fall_never_reach_the_frame()
{
    Rig rig;
    const uint8_t stray[2] = {0xAA, 0x55};
    rig.line.arrive(stray, 2);
    rig.line.strayBeforeFall = 2;
    rig.line.arrive(kRead1.data(), kRead1.size());
    const auto received = rig.transport.waitCommandFrame(20000);
    TEST_ASSERT_TRUE(received.ok());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kRead1.data(), received.value().bytes.data(), 5);
    TEST_ASSERT_EQUAL_UINT(1, rig.line.flushes);
}

void test_a_short_frame_is_passed_on_for_the_session_to_ignore()
{
    Rig rig;
    rig.line.arrive(kRead1.data(), 3);
    const auto received = rig.transport.waitCommandFrame(20000);
    TEST_ASSERT_TRUE(received.ok());
    TEST_ASSERT_EQUAL_UINT8(3, received.value().count);
    TEST_ASSERT_EQUAL_UINT32(1, rig.transport.stats().shortFrames);
    TEST_ASSERT_EQUAL_UINT32(0, rig.transport.stats().frames);
}

void test_command_stuck_low_is_an_error_and_releases_data_in()
{
    Rig rig;
    rig.line.arrive(kRead1.data(), kRead1.size());
    rig.line.risesIn = false;
    const auto received = rig.transport.waitCommandFrame(20000);
    TEST_ASSERT_FALSE(received.ok());
    TEST_ASSERT_EQUAL(ErrorCode::TransportError, received.error());
    TEST_ASSERT_EQUAL_UINT(1, rig.line.idles);
    TEST_ASSERT_EQUAL_UINT(2, rig.line.flushes);  // at the fall, and the recovery
    TEST_ASSERT_EQUAL_UINT32(1, rig.transport.stats().stuckLows);
}

void test_a_receive_error_is_an_error_and_releases_data_in()
{
    Rig rig;
    rig.line.readFails = true;
    const auto received = rig.transport.waitCommandFrame(20000);
    TEST_ASSERT_FALSE(received.ok());
    TEST_ASSERT_EQUAL(ErrorCode::TransportError, received.error());
    TEST_ASSERT_EQUAL_UINT(1, rig.line.idles);
    TEST_ASSERT_EQUAL_UINT32(1, rig.transport.stats().receiveErrors);
}

void test_the_first_reply_records_the_command_latency()
{
    Rig rig;
    rig.line.arrive(kRead1.data(), kRead1.size());
    TEST_ASSERT_TRUE(rig.transport.waitCommandFrame(20000).ok());
    rig.line.writeStartDelayUs = 0;
    rig.clock.advance(1200);  // the session waits t2 before the ACK
    const uint8_t ack = 0x41;
    TEST_ASSERT_TRUE(rig.transport.send(Span<const uint8_t>{&ack, 1}).ok());
    TEST_ASSERT_EQUAL_UINT32(1200, rig.transport.stats().lastAckLatencyUs);
    const uint8_t complete = 0x43;
    rig.clock.advance(5000);
    TEST_ASSERT_TRUE(rig.transport.send(Span<const uint8_t>{&complete, 1}).ok());
    TEST_ASSERT_EQUAL_UINT32(1200, rig.transport.stats().lastAckLatencyUs);  // only the first send counts
    TEST_ASSERT_EQUAL_UINT32(1200, rig.transport.stats().maxAckLatencyUs);
    TEST_ASSERT_EQUAL_UINT(2, rig.line.writtenSize);
}

void test_a_failed_send_releases_data_in()
{
    Rig rig;
    rig.line.writeFails = true;
    const uint8_t ack = 0x41;
    TEST_ASSERT_FALSE(rig.transport.send(Span<const uint8_t>{&ack, 1}).ok());
    TEST_ASSERT_EQUAL_UINT(1, rig.line.idles);
    TEST_ASSERT_EQUAL_UINT32(1, rig.transport.stats().sendErrors);
}

void test_release_data_line_always_idles_the_line()
{
    Rig rig;
    rig.transport.releaseDataLine();
    rig.transport.releaseDataLine();
    TEST_ASSERT_EQUAL_UINT(2, rig.line.idles);
    TEST_ASSERT_EQUAL_UINT32(2, rig.transport.stats().releases);
}

void test_receive_after_ack_sends_the_ack_then_reads_the_data_frame()
{
    Rig rig;
    std::array<uint8_t, 129> data{};
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>(i);
    }
    rig.line.arrive(data.data(), data.size());
    std::array<uint8_t, 129> frame{};
    TEST_ASSERT_TRUE(rig.transport.receiveAfterAck(Span<uint8_t>{frame}).ok());
    TEST_ASSERT_EQUAL_UINT(1, rig.line.writtenSize);
    TEST_ASSERT_EQUAL_UINT8(0x41, rig.line.written[0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data.data(), frame.data(), frame.size());
    TEST_ASSERT_EQUAL_UINT32(SioLineTransport::byteTimeUs(129) + SioLineTransport::kDataMarginUs,
                             rig.line.lastReadTimeoutUs);
}

void test_a_short_data_frame_is_an_error_and_releases_data_in()
{
    Rig rig;
    const uint8_t some[10] = {};
    rig.line.arrive(some, sizeof some);
    std::array<uint8_t, 129> frame{};
    TEST_ASSERT_FALSE(rig.transport.receiveAfterAck(Span<uint8_t>{frame}).ok());
    TEST_ASSERT_EQUAL_UINT(1, rig.line.idles);
    TEST_ASSERT_EQUAL_UINT32(1, rig.transport.stats().receiveErrors);
}

void test_byte_time_matches_19200_baud()
{
    TEST_ASSERT_EQUAL_UINT32(521, SioLineTransport::byteTimeUs(1));
    TEST_ASSERT_EQUAL_UINT32(2605, SioLineTransport::byteTimeUs(5));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_no_command_is_a_timeout_and_touches_nothing);
    RUN_TEST(test_a_frame_in_the_window_is_returned_with_both_edges);
    RUN_TEST(test_bytes_before_the_fall_never_reach_the_frame);
    RUN_TEST(test_a_short_frame_is_passed_on_for_the_session_to_ignore);
    RUN_TEST(test_command_stuck_low_is_an_error_and_releases_data_in);
    RUN_TEST(test_a_receive_error_is_an_error_and_releases_data_in);
    RUN_TEST(test_the_first_reply_records_the_command_latency);
    RUN_TEST(test_a_failed_send_releases_data_in);
    RUN_TEST(test_release_data_line_always_idles_the_line);
    RUN_TEST(test_receive_after_ack_sends_the_ack_then_reads_the_data_frame);
    RUN_TEST(test_a_short_data_frame_is_an_error_and_releases_data_in);
    RUN_TEST(test_byte_time_matches_19200_baud);
    return UNITY_END();
}
