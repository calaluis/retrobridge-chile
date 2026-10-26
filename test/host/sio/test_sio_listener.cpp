// SioListener: passive classification and recording of command frames (H5,
// second host-only deliverable; docs/diseno/h5-escucha-solo-recepcion.md
// sections 5 and 6). Pure logic over ScriptedSioTransport and FakeClock: these
// tests say nothing about the electrical side of the bus (H4, M3-M5).
//
// Wait times are arbitrary test values, not bus timing.

#include <array>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "fakes/fake_clock.hpp"
#include "fakes/scripted_sio_transport.hpp"
#include "rb/app/ports/sio_command_source.hpp"
#include "rb/app/ports/sio_transport.hpp"
#include "rb/core/span.hpp"
#include "rb/sio/sio_checksum.hpp"
#include "rb/sio/sio_listener.hpp"
#include "unity.h"

using rb::sio::ListenOutcome;
using rb::sio::ListenRecord;
using rb::sio::ListenStats;
using rb::sio::saturating_increment;
using rb::sio::SioListener;
using rb::test::FakeClock;
using rb::test::ScriptedSioTransport;

namespace {

constexpr uint32_t kWaitUs = 20000;  // test value

// --- Passivity by construction ------------------------------------------------------
// The listener holds only SioCommandSource, which has no transmit operations.
template <typename T, typename = void>
struct HasSend : std::false_type {};
template <typename T>
struct HasSend<T, std::void_t<decltype(std::declval<T&>().send(std::declval<rb::core::Span<const uint8_t>>()))>>
    : std::true_type {};

template <typename T, typename = void>
struct HasReleaseDataLine : std::false_type {};
template <typename T>
struct HasReleaseDataLine<T, std::void_t<decltype(std::declval<T&>().releaseDataLine())>> : std::true_type {};

static_assert(!HasSend<rb::app::SioCommandSource>::value, "the receive-only port must not transmit");
static_assert(!HasReleaseDataLine<rb::app::SioCommandSource>::value, "the receive-only port must not touch DATA IN");
static_assert(HasSend<rb::app::SioTransport>::value && HasReleaseDataLine<rb::app::SioTransport>::value,
              "detector sanity check: the full transport does have them");
static_assert(std::is_constructible<SioListener, rb::app::SioCommandSource&, const rb::app::Clock&>::value,
              "the listener is built from the receive-only port");

// A frame with a correct checksum (sio_checksum is the single implementation).
std::array<uint8_t, 5> valid_frame(uint8_t device, uint8_t command, uint8_t aux1 = 0, uint8_t aux2 = 0)
{
    std::array<uint8_t, 5> bytes{device, command, aux1, aux2, 0};
    bytes[4] = rb::sio::sio_checksum(rb::core::Span<const uint8_t>{bytes.data(), 4});
    return bytes;
}

struct Harness {
    FakeClock clock;
    ScriptedSioTransport transport{clock};
    SioListener listener{transport, clock};

    void assert_passive() const
    {
        TEST_ASSERT_EQUAL_UINT(0, transport.sends());
        TEST_ASSERT_EQUAL_UINT(0, transport.releases());
    }
};

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_saturating_increment_stops_at_the_limit(void)
{
    TEST_ASSERT_EQUAL_UINT32(1, saturating_increment(0));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, saturating_increment(UINT32_MAX - 1));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, saturating_increment(UINT32_MAX));
}

static void test_consecutive_valid_frames_are_recorded_in_order(void)
{
    Harness h;
    h.transport.push_frame(valid_frame(0x31, 0x53), 5, 3000);
    h.transport.push_frame(valid_frame(0x31, 0x52, 0x01), 5, 4000);
    h.transport.push_frame(valid_frame(0x32, 0x52, 0x68, 0x01), 5, 5000);

    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_TRUE(h.listener.step(kWaitUs) == ListenOutcome::Frame);
    }
    TEST_ASSERT_EQUAL_UINT(3, h.listener.size());
    TEST_ASSERT_EQUAL_UINT32(3, h.listener.stats().frames);

    const ListenRecord& first = h.listener.at(0);
    TEST_ASSERT_EQUAL_UINT32(1, first.sequence);
    TEST_ASSERT_EQUAL_UINT8(0x53, first.bytes[1]);
    TEST_ASSERT_EQUAL_UINT64(3000, first.atUs);  // COMMAND release
    TEST_ASSERT_EQUAL_UINT64(7000, h.listener.at(1).atUs);
    TEST_ASSERT_EQUAL_UINT64(12000, h.listener.at(2).atUs);
    TEST_ASSERT_EQUAL_UINT8(0x32, h.listener.at(2).bytes[0]);
    TEST_ASSERT_EQUAL_UINT32(3, h.listener.at(2).sequence);
    h.assert_passive();
}

static void test_wrong_checksum_is_recorded_as_received(void)
{
    Harness h;
    auto bytes = valid_frame(0x31, 0x52, 0x01);
    bytes[4] ^= 0xFF;
    h.transport.push_frame(bytes);

    TEST_ASSERT_TRUE(h.listener.step(kWaitUs) == ListenOutcome::ChecksumMismatch);
    TEST_ASSERT_EQUAL_UINT32(1, h.listener.stats().checksumMismatches);
    TEST_ASSERT_EQUAL_UINT32(0, h.listener.stats().frames);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), h.listener.at(0).bytes.data(), 5);
    h.assert_passive();
}

static void test_zero_to_four_bytes_are_incomplete(void)
{
    Harness h;
    for (uint8_t count = 0; count <= 4; ++count) {
        h.transport.push_frame(valid_frame(0x31, 0x53), count);
    }
    for (uint8_t count = 0; count <= 4; ++count) {
        TEST_ASSERT_TRUE(h.listener.step(kWaitUs) == ListenOutcome::Incomplete);
        TEST_ASSERT_EQUAL_UINT8(count, h.listener.at(count).count);
    }
    TEST_ASSERT_EQUAL_UINT32(5, h.listener.stats().incomplete);
    TEST_ASSERT_EQUAL_UINT32(0, h.listener.stats().frames);
    h.assert_passive();
}

static void test_more_than_five_bytes_are_overlong(void)
{
    Harness h;
    const auto bytes = valid_frame(0x31, 0x53);  // a valid checksum does not make it a frame
    h.transport.push_frame(bytes, 6);
    h.transport.push_frame(bytes, 255);

    TEST_ASSERT_TRUE(h.listener.step(kWaitUs) == ListenOutcome::Overlong);
    TEST_ASSERT_TRUE(h.listener.step(kWaitUs) == ListenOutcome::Overlong);
    TEST_ASSERT_EQUAL_UINT8(6, h.listener.at(0).count);
    TEST_ASSERT_EQUAL_UINT8(255, h.listener.at(1).count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes.data(), h.listener.at(1).bytes.data(), 5);  // first 5 only
    TEST_ASSERT_EQUAL_UINT32(2, h.listener.stats().overlong);
    TEST_ASSERT_EQUAL_UINT32(0, h.listener.stats().frames);
    h.assert_passive();
}

static void test_timeouts_are_counted_but_not_recorded(void)
{
    Harness h;
    h.transport.push_timeout();
    TEST_ASSERT_TRUE(h.listener.step(kWaitUs) == ListenOutcome::Timeout);
    TEST_ASSERT_TRUE(h.listener.step(kWaitUs) == ListenOutcome::Timeout);  // empty script: also a timeout
    TEST_ASSERT_EQUAL_UINT32(2, h.listener.stats().timeouts);
    TEST_ASSERT_EQUAL_UINT(0, h.listener.size());
    h.assert_passive();
}

static void test_transport_errors_are_recorded_with_the_clock_time(void)
{
    Harness h;
    h.clock.advance(1234);
    h.transport.push_receive_error();
    TEST_ASSERT_TRUE(h.listener.step(kWaitUs) == ListenOutcome::TransportError);
    TEST_ASSERT_EQUAL_UINT32(1, h.listener.stats().transportErrors);
    TEST_ASSERT_EQUAL_UINT(1, h.listener.size());
    TEST_ASSERT_TRUE(h.listener.at(0).outcome == ListenOutcome::TransportError);
    TEST_ASSERT_EQUAL_UINT8(0, h.listener.at(0).count);
    TEST_ASSERT_EQUAL_UINT64(1234, h.listener.at(0).atUs);
    h.assert_passive();
}

static void test_mixed_events_keep_their_order(void)
{
    Harness h;
    auto bad = valid_frame(0x31, 0x52);
    bad[4] ^= 0x01;
    h.transport.push_frame(valid_frame(0x31, 0x53));
    h.transport.push_timeout();
    h.transport.push_frame(bad);
    h.transport.push_receive_error();
    h.transport.push_frame(valid_frame(0x31, 0x52), 3);
    h.transport.push_frame(valid_frame(0x31, 0x52), 9);

    const ListenOutcome expected[] = {ListenOutcome::Frame,          ListenOutcome::Timeout,
                                      ListenOutcome::ChecksumMismatch, ListenOutcome::TransportError,
                                      ListenOutcome::Incomplete,     ListenOutcome::Overlong};
    for (ListenOutcome outcome : expected) {
        TEST_ASSERT_TRUE(h.listener.step(kWaitUs) == outcome);
    }
    const ListenOutcome recorded[] = {ListenOutcome::Frame, ListenOutcome::ChecksumMismatch,
                                      ListenOutcome::TransportError, ListenOutcome::Incomplete,
                                      ListenOutcome::Overlong};
    TEST_ASSERT_EQUAL_UINT(5, h.listener.size());
    for (std::size_t i = 0; i < 5; ++i) {
        TEST_ASSERT_TRUE(h.listener.at(i).outcome == recorded[i]);
        TEST_ASSERT_EQUAL_UINT32(i + 1, h.listener.at(i).sequence);
        if (i > 0) {
            TEST_ASSERT_TRUE(h.listener.at(i).atUs >= h.listener.at(i - 1).atUs);
        }
    }
    h.assert_passive();
}

static void test_full_ring_overwrites_the_oldest(void)
{
    Harness h;
    constexpr std::size_t kEvents = SioListener::kCapacity + 3;  // 35
    for (std::size_t i = 0; i < kEvents; ++i) {
        h.transport.push_frame(valid_frame(0x31, 0x52, static_cast<uint8_t>(i)));
    }
    for (std::size_t i = 0; i < kEvents; ++i) {
        TEST_ASSERT_TRUE(h.listener.step(kWaitUs) == ListenOutcome::Frame);
    }
    TEST_ASSERT_EQUAL_UINT(SioListener::kCapacity, h.listener.size());
    TEST_ASSERT_EQUAL_UINT32(3, h.listener.stats().overwritten);
    TEST_ASSERT_EQUAL_UINT32(kEvents, h.listener.stats().frames);
    TEST_ASSERT_EQUAL_UINT32(4, h.listener.at(0).sequence);  // records 1-3 were overwritten
    TEST_ASSERT_EQUAL_UINT8(3, h.listener.at(0).bytes[2]);
    TEST_ASSERT_EQUAL_UINT32(kEvents, h.listener.at(SioListener::kCapacity - 1).sequence);
    for (std::size_t i = 1; i < SioListener::kCapacity; ++i) {
        TEST_ASSERT_EQUAL_UINT32(h.listener.at(i - 1).sequence + 1, h.listener.at(i).sequence);
    }
    h.assert_passive();
}

static void test_reset_stats_keeps_records_and_clear_keeps_stats(void)
{
    Harness h;
    h.transport.push_frame(valid_frame(0x31, 0x53));
    h.transport.push_timeout();
    h.transport.push_frame(valid_frame(0x31, 0x52));
    h.listener.step(kWaitUs);
    h.listener.step(kWaitUs);

    h.listener.resetStats();
    TEST_ASSERT_EQUAL_UINT32(0, h.listener.stats().frames);
    TEST_ASSERT_EQUAL_UINT32(0, h.listener.stats().timeouts);
    TEST_ASSERT_EQUAL_UINT(1, h.listener.size());

    TEST_ASSERT_TRUE(h.listener.step(kWaitUs) == ListenOutcome::Frame);
    TEST_ASSERT_EQUAL_UINT32(1, h.listener.stats().frames);
    h.listener.clearRecords();
    TEST_ASSERT_EQUAL_UINT(0, h.listener.size());
    TEST_ASSERT_EQUAL_UINT32(1, h.listener.stats().frames);

    h.transport.push_frame(valid_frame(0x31, 0x53));
    TEST_ASSERT_TRUE(h.listener.step(kWaitUs) == ListenOutcome::Frame);
    TEST_ASSERT_EQUAL_UINT(1, h.listener.size());
    TEST_ASSERT_EQUAL_UINT32(3, h.listener.at(0).sequence);  // the sequence continues
    h.assert_passive();
}

static void test_same_script_gives_the_same_records(void)
{
    Harness a;
    Harness b;
    for (Harness* h : {&a, &b}) {
        h->transport.push_frame(valid_frame(0x31, 0x53), 5, 2500);
        h->transport.push_receive_error();
        h->transport.push_frame(valid_frame(0x31, 0x52, 7), 4, 1800);
        for (int i = 0; i < 3; ++i) {
            h->listener.step(kWaitUs);
        }
    }
    TEST_ASSERT_EQUAL_UINT(a.listener.size(), b.listener.size());
    for (std::size_t i = 0; i < a.listener.size(); ++i) {
        const ListenRecord& x = a.listener.at(i);
        const ListenRecord& y = b.listener.at(i);
        TEST_ASSERT_TRUE(x.outcome == y.outcome);
        TEST_ASSERT_EQUAL_UINT32(x.sequence, y.sequence);
        TEST_ASSERT_EQUAL_UINT8(x.count, y.count);
        TEST_ASSERT_EQUAL_UINT64(x.atUs, y.atUs);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(x.bytes.data(), y.bytes.data(), 5);
    }
    a.assert_passive();
    b.assert_passive();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_saturating_increment_stops_at_the_limit);
    RUN_TEST(test_consecutive_valid_frames_are_recorded_in_order);
    RUN_TEST(test_wrong_checksum_is_recorded_as_received);
    RUN_TEST(test_zero_to_four_bytes_are_incomplete);
    RUN_TEST(test_more_than_five_bytes_are_overlong);
    RUN_TEST(test_timeouts_are_counted_but_not_recorded);
    RUN_TEST(test_transport_errors_are_recorded_with_the_clock_time);
    RUN_TEST(test_mixed_events_keep_their_order);
    RUN_TEST(test_full_ring_overwrites_the_oldest);
    RUN_TEST(test_reset_stats_keeps_records_and_clear_keeps_stats);
    RUN_TEST(test_same_script_gives_the_same_records);
    return UNITY_END();
}
