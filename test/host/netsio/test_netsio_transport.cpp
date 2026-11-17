// NetSioSioTransport under the real SioSession (ADR-016, Issue #32 E1a, level A).
// A ScriptedDatagramPort plays the NetSIO hub; times are simulated (FakeClock),
// so these tests check the datagram sequence, not SIO or network timing.

#include <array>
#include <cstdint>

#include "fakes/fake_byte_source.hpp"
#include "netsio/netsio_harness.hpp"
#include "rb/sio/sio_codes.hpp"
#include "support/atr_header.hpp"
#include "unity.h"

using rb::core::ErrorCode;
using rb::core::Span;
using rb::netsio::NetSioConfig;
using rb::netsio::PeerProfile;
using rb::sio::StepOutcome;
using rb::test::command_frame;
using rb::test::FakeByteSource;
using rb::test::kAtrHeaderSize;
using rb::test::kD1;
using rb::test::kD2;
using rb::test::make_atr_header;
using rb::test::NetSioHarness;
using rb::test::sd_data_size;

namespace {

FakeByteSource sd_image()
{
    return FakeByteSource{make_atr_header(sd_data_size(720), 128), kAtrHeaderSize + sd_data_size(720)};
}

const std::array<uint8_t, 5> kStatusD1 = command_frame(kD1, rb::sio::command::kStatus);  // 31 53 00 00 84

// STATUS of a mounted single-density disk: drive 00, FDC $FF, timeout $E0, 00
// (StatusFrameEncoder), checksum $E0.
void assert_status_reply(const NetSioHarness& h, std::size_t first, uint8_t sync)
{
    h.assert_sent(first, {0x81, sync, 0x01, 0x41, 0x00, 0x00});
    h.assert_sent(first + 1, {0x02, 0x43, 0x00, 0xFF, 0xE0, 0x00, 0xE0});
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- Link --------------------------------------------------------------------------

static void test_connect_pings_then_registers(void)
{
    NetSioHarness h;
    h.port.push({0xC3});
    TEST_ASSERT_TRUE(h.transport.connect().ok());
    TEST_ASSERT_EQUAL_size_t(2, h.port.sent_count());
    h.assert_sent(0, {0xC2});
    h.assert_sent(1, {0xC1});
}

static void test_connect_retries_and_gives_up(void)
{
    NetSioHarness h;  // pingAttempts = 3, nobody answers
    const auto result = h.transport.connect();
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_TRUE(result.error() == ErrorCode::Timeout);
    TEST_ASSERT_EQUAL_size_t(3, h.port.sent_count());
    TEST_ASSERT_FALSE(h.transport.connected());
}

static void test_connect_waits_out_a_refused_ping(void)
{
    NetSioHarness h;
    h.port.push_receive_error();  // Windows: ICMP port unreachable, hub not started yet
    h.port.push({0xC3});
    const uint64_t start = h.clock.nowMicros();
    TEST_ASSERT_TRUE(h.transport.connect().ok());
    TEST_ASSERT_TRUE(h.clock.nowMicros() - start >= NetSioConfig{}.pingTimeoutUs);  // no busy retry
    h.assert_sent(0, {0xC2});
    h.assert_sent(1, {0xC2});
    h.assert_sent(2, {0xC1});
}

static void test_disconnect_sends_c0_once(void)
{
    NetSioHarness h;
    h.connect();
    h.transport.disconnect();
    h.transport.disconnect();
    TEST_ASSERT_EQUAL_size_t(1, h.port.sent_count());
    h.assert_sent(0, {0xC0});
}

static void test_alive_requests_keep_the_registration(void)
{
    NetSioConfig config = rb::test::harness_config(PeerProfile::Fep003Strict);
    config.aliveIntervalUs = 10000;  // 10 ms, so a 20 ms idle poll must send C4
    NetSioHarness h{config};
    h.connect();
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Idle);
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Idle);
    std::size_t alive = 0;
    for (std::size_t i = 0; i < h.port.sent_count(); ++i) {
        alive += h.port.sent(i).bytes[0] == 0xC4 ? 1u : 0u;
    }
    TEST_ASSERT_TRUE(alive >= 3);
    TEST_ASSERT_EQUAL_UINT32(alive, h.transport.stats().aliveRequests);
}

// --- Transactions --------------------------------------------------------------------

static void test_status_is_acked_in_the_sync_response_then_completed_as_a_data_block(void)
{
    NetSioHarness h;
    FakeByteSource sd = sd_image();
    h.mount(sd);
    h.connect(3);
    h.push_frame(kStatusD1, 0x07);

    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);
    TEST_ASSERT_EQUAL_size_t(2, h.port.sent_count());
    assert_status_reply(h, 0, 0x07);
    TEST_ASSERT_EQUAL_UINT8(2, h.transport.credit());  // the data block used one credit
    TEST_ASSERT_FALSE(h.transport.syncPending());
    TEST_ASSERT_EQUAL_UINT32(1, h.transport.stats().validSyncResponses);
}

static void test_hub_profile_serves_the_frame_captured_in_e0(void)
{
    NetSioHarness h{PeerProfile::NetsiohubTrailingByte};
    FakeByteSource sd = sd_image();
    h.mount(sd);
    h.connect();
    // Exactly the datagrams of the first STATUS in E0 run b4 (trailers 23..26).
    h.port.push({0x80, 0x60, 0x4A, 0x00, 0x00, 0x23});
    h.port.push({0x11, 0x24});
    h.port.push({0x02, 0x31, 0x53, 0x00, 0x00, 0x84, 0x25});
    h.port.push({0x18, 0x51, 0x26});

    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);
    assert_status_reply(h, 0, 0x51);
    TEST_ASSERT_EQUAL_UINT32(19040, h.transport.stats().lastBaud);
    TEST_ASSERT_EQUAL_UINT32(0, h.transport.stats().trailerGaps);
}

static void test_read_sector_sends_the_sector_and_its_checksum(void)
{
    NetSioHarness h;
    FakeByteSource sd = sd_image();
    h.mount(sd);
    h.connect();
    h.push_frame(command_frame(kD1, rb::sio::command::kReadSector, 1, 0), 0x08);

    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);
    h.assert_sent(0, {0x81, 0x08, 0x01, 0x41, 0x00, 0x00});
    const auto& block = h.port.sent(1);
    TEST_ASSERT_EQUAL_size_t(1 + 1 + 128 + 1, block.length);  // ID, 'C', sector, checksum
    TEST_ASSERT_EQUAL_HEX8(0x02, block.bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(rb::sio::code::kComplete, block.bytes[1]);
    for (uint32_t i = 0; i < 128; ++i) {
        TEST_ASSERT_EQUAL_HEX8(FakeByteSource::data_byte(kAtrHeaderSize + i), block.bytes[2 + i]);
    }
    TEST_ASSERT_EQUAL_HEX8(rb::sio::sio_checksum(Span<const uint8_t>{block.bytes.data() + 2, 128}),
                           block.bytes[2 + 128]);
}

static void test_unsupported_command_is_nacked_in_the_sync_response(void)
{
    NetSioHarness h;
    FakeByteSource sd = sd_image();
    h.mount(sd);
    h.connect();
    h.push_frame(command_frame(kD1, 0x50 /* PUT: v1 is read-only */, 1, 0), 0x09);

    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Nacked);
    TEST_ASSERT_EQUAL_size_t(1, h.port.sent_count());
    h.assert_sent(0, {0x81, 0x09, 0x01, 0x4E, 0x00, 0x00});
}

static void test_frame_for_another_device_gets_an_empty_sync_response(void)
{
    NetSioHarness h;
    h.connect();
    h.push_frame(command_frame(kD2, rb::sio::command::kStatus), 0x0A);

    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Ignored);
    TEST_ASSERT_EQUAL_size_t(1, h.port.sent_count());
    h.assert_sent(0, {0x81, 0x0A, 0x00, 0x00, 0x00, 0x00});
    TEST_ASSERT_EQUAL_UINT32(1, h.transport.stats().emptySyncResponses);
}

static void test_corrupted_frame_gets_an_empty_sync_response(void)
{
    NetSioHarness h;
    h.connect();
    std::array<uint8_t, 5> corrupted = kStatusD1;
    corrupted[4] ^= 0xFF;
    h.push_frame(corrupted, 0x0B);

    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Ignored);
    h.assert_sent(0, {0x81, 0x0B, 0x00, 0x00, 0x00, 0x00});
}

static void test_status_of_an_empty_drive_is_still_answered(void)
{
    NetSioHarness h;  // nothing mounted: STATUS is accepted and reports "not ready"
    h.connect();
    h.push_frame(kStatusD1, 0x0C);
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);
    h.assert_sent(0, {0x81, 0x0C, 0x01, 0x41, 0x00, 0x00});
    TEST_ASSERT_EQUAL_HEX8(0x43, h.port.sent(1).bytes[1]);
}

static void test_frame_sent_byte_by_byte_is_assembled(void)
{
    NetSioHarness h{PeerProfile::NetsiohubTrailingByte};
    FakeByteSource sd = sd_image();
    h.mount(sd);
    h.connect();
    h.from_atari({0x11});
    for (uint8_t byte : kStatusD1) {
        h.from_atari({0x01, byte});
    }
    h.from_atari({0x18, 0x0D});

    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);
    assert_status_reply(h, 0, 0x0D);
}

static void test_short_frame_is_ignored_and_released(void)
{
    NetSioHarness h;
    h.connect();
    h.from_atari({0x11});
    h.from_atari({0x02, 0x31, 0x53, 0x00});
    h.from_atari({0x18, 0x0E});

    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Ignored);
    h.assert_sent(0, {0x81, 0x0E, 0x00, 0x00, 0x00, 0x00});
}

// --- Profiles and robustness -----------------------------------------------------------

static void test_strict_profile_does_not_answer_hub_framed_messages(void)
{
    NetSioHarness h{PeerProfile::Fep003Strict};
    h.connect();
    h.port.push({0x11, 0x24});
    h.port.push({0x02, 0x31, 0x53, 0x00, 0x00, 0x84, 0x25});
    h.port.push({0x18, 0x51, 0x26});

    // E0's original failure, now explicit: wrong profile, nothing understood.
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Idle);
    TEST_ASSERT_EQUAL_UINT32(2, h.transport.stats().malformed);  // 11 24 and 18 51 26
    TEST_ASSERT_EQUAL_UINT32(0, h.transport.stats().validSyncResponses);
}

static void test_cold_reset_discards_a_partial_frame(void)
{
    NetSioHarness h{PeerProfile::NetsiohubTrailingByte};
    FakeByteSource sd = sd_image();
    h.mount(sd);
    h.connect();
    h.from_atari({0x11});
    h.from_atari({0x01, 0x31});
    h.from_atari({0xFF});  // cold reset in the middle of the frame
    h.push_frame(kStatusD1, 0x0F);

    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);
    assert_status_reply(h, 0, 0x0F);
    TEST_ASSERT_EQUAL_UINT32(1, h.transport.stats().resets);
    TEST_ASSERT_EQUAL_UINT32(1, h.transport.stats().framesDiscarded);
    TEST_ASSERT_EQUAL_UINT32(1, h.transport.stats().framesDelivered);
}

static void test_sync_without_frame_is_released_at_once(void)
{
    NetSioHarness h;
    h.connect();
    h.from_atari({0x18, 0x10});
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Idle);
    h.assert_sent(0, {0x81, 0x10, 0x00, 0x00, 0x00, 0x00});
}

static void test_write_data_sync_is_released_empty(void)
{
    NetSioHarness h;
    h.connect();
    h.from_atari({0x09, 0x5A, 0x11});  // DATA BYTE + SYNC: never planned by a read-only drive
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Idle);
    h.assert_sent(0, {0x81, 0x11, 0x00, 0x00, 0x00, 0x00});
}

static void test_trailer_gaps_are_counted(void)
{
    NetSioHarness h{PeerProfile::NetsiohubTrailingByte};
    h.connect();
    h.port.push({0xFF, 0x10});
    h.port.push({0xFF, 0x11});
    h.port.push({0xFF, 0x15});  // gap
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Idle);
    TEST_ASSERT_EQUAL_UINT32(1, h.transport.stats().trailerGaps);
    TEST_ASSERT_EQUAL_UINT32(3, h.transport.stats().resets);
}

// --- Credit ----------------------------------------------------------------------------

static void test_without_credit_the_transport_asks_and_waits(void)
{
    NetSioHarness h;
    FakeByteSource sd = sd_image();
    h.mount(sd);
    h.connect(0);  // no C7 yet
    h.push_frame(kStatusD1, 0x12);
    h.port.push({0xC7, 0x01}, 2000);  // credit arrives 2 ms after the request

    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);
    TEST_ASSERT_EQUAL_size_t(3, h.port.sent_count());
    h.assert_sent(0, {0x81, 0x12, 0x01, 0x41, 0x00, 0x00});
    h.assert_sent(1, {0xC6, 0x00});
    h.assert_sent(2, {0x02, 0x43, 0x00, 0xFF, 0xE0, 0x00, 0xE0});
    TEST_ASSERT_EQUAL_UINT8(0, h.transport.credit());
    TEST_ASSERT_EQUAL_UINT32(1, h.transport.stats().creditRequests);
}

// P1 (Issue #32): credit granted to a previous registration must not survive a
// disconnect. The hub grants credit per connection; after C0 and a new C1 with no
// fresh C7, the COMPLETE must wait for credit (C6) instead of spending the old one.
static void test_reconnect_without_new_credit_asks_before_completing(void)
{
    NetSioHarness h;
    FakeByteSource sd = sd_image();
    h.mount(sd);
    h.connect(3);  // C7 = 3 on the first registration
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Idle);  // the idle wait takes in the C7
    TEST_ASSERT_EQUAL_UINT8(3, h.transport.credit());
    h.transport.disconnect();
    h.connect(0);  // same instance: C3 again, no new C7
    h.push_frame(kStatusD1, 0x15);
    h.port.push({0xC7, 0x01}, 2000);

    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);
    TEST_ASSERT_EQUAL_size_t(3, h.port.sent_count());
    h.assert_sent(0, {0x81, 0x15, 0x01, 0x41, 0x00, 0x00});
    h.assert_sent(1, {0xC6, 0x00});
    h.assert_sent(2, {0x02, 0x43, 0x00, 0xFF, 0xE0, 0x00, 0xE0});
    TEST_ASSERT_EQUAL_UINT8(0, h.transport.credit());
    TEST_ASSERT_EQUAL_UINT32(1, h.transport.stats().creditRequests);
}

// P1 review: a frame cut by a disconnect must not be completed by a sync that
// arrives after the next registration.
static void test_partial_frame_does_not_survive_a_reconnect(void)
{
    NetSioHarness h;
    FakeByteSource sd = sd_image();
    h.mount(sd);
    h.connect(3);
    h.from_atari({0x11});
    h.from_atari({0x02, 0x31, 0x53, 0x00, 0x00, 0x84});
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Idle);  // frame open, no sync yet
    h.transport.disconnect();
    h.connect(3);
    h.from_atari({0x18, 0x16});  // sync of the new session, with no COMMAND ON before it

    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Idle);
    TEST_ASSERT_EQUAL_size_t(1, h.port.sent_count());
    h.assert_sent(0, {0x81, 0x16, 0x00, 0x00, 0x00, 0x00});
}

static void test_credit_timeout_drops_the_transaction_and_the_next_one_succeeds(void)
{
    NetSioHarness h;
    FakeByteSource sd = sd_image();
    h.mount(sd);
    h.connect(0);
    h.push_frame(kStatusD1, 0x13);

    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::TransportFailed);
    TEST_ASSERT_FALSE(h.dispatcher.hasPendingCommand());
    TEST_ASSERT_FALSE(h.transport.syncPending());  // the ACK had already answered the sync

    h.port.push({0xC7, 0x03});
    h.push_frame(kStatusD1, 0x14);
    h.port.clear_sent();
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);
    assert_status_reply(h, 0, 0x14);
}

// --- Port failures ---------------------------------------------------------------------

static void test_failed_ack_is_released_with_an_empty_sync_response(void)
{
    NetSioHarness h;
    FakeByteSource sd = sd_image();
    h.mount(sd);
    h.connect();          // send attempts 0 (C2) and 1 (C1)
    h.port.fail_send(2);  // the ACK
    h.push_frame(kStatusD1, 0x15);

    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::TransportFailed);
    TEST_ASSERT_FALSE(h.dispatcher.hasPendingCommand());
    TEST_ASSERT_EQUAL_size_t(1, h.port.sent_count());
    h.assert_sent(0, {0x81, 0x15, 0x00, 0x00, 0x00, 0x00});
}

static void test_receive_error_is_a_transport_failure(void)
{
    NetSioHarness h;
    h.connect();
    h.port.push_receive_error();
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::TransportFailed);
}

static void test_answering_a_sync_with_more_than_one_byte_is_refused(void)
{
    NetSioHarness h;
    h.connect();
    h.push_frame(kStatusD1, 0x16);
    TEST_ASSERT_TRUE(h.transport.waitCommandFrame(20000).ok());
    TEST_ASSERT_TRUE(h.transport.syncPending());
    const uint8_t two[] = {0x41, 0x43};
    const auto sent = h.transport.send(Span<const uint8_t>{two});
    TEST_ASSERT_FALSE(sent.ok());
    TEST_ASSERT_TRUE(sent.error() == ErrorCode::TransportError);
    h.transport.releaseDataLine();
    TEST_ASSERT_EQUAL_size_t(1, h.port.sent_count());
    h.assert_sent(0, {0x81, 0x16, 0x00, 0x00, 0x00, 0x00});
}

static void test_command_bytes_carry_pc_clock_edges(void)
{
    NetSioHarness h;
    h.connect();
    h.port.push({0x11}, 1000);
    h.port.push({0x02, 0x31, 0x53, 0x00, 0x00, 0x84}, 500);
    h.port.push({0x18, 0x17}, 500);
    const uint64_t start = h.clock.nowMicros();
    const auto frame = h.transport.waitCommandFrame(20000);
    TEST_ASSERT_TRUE(frame.ok());
    TEST_ASSERT_EQUAL_UINT8(5, frame.value().count);
    TEST_ASSERT_EQUAL_UINT64(start + 1000, frame.value().commandAssertedAtUs);
    TEST_ASSERT_EQUAL_UINT64(start + 2000, frame.value().commandReleasedAtUs);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_connect_pings_then_registers);
    RUN_TEST(test_connect_retries_and_gives_up);
    RUN_TEST(test_connect_waits_out_a_refused_ping);
    RUN_TEST(test_disconnect_sends_c0_once);
    RUN_TEST(test_alive_requests_keep_the_registration);
    RUN_TEST(test_status_is_acked_in_the_sync_response_then_completed_as_a_data_block);
    RUN_TEST(test_hub_profile_serves_the_frame_captured_in_e0);
    RUN_TEST(test_read_sector_sends_the_sector_and_its_checksum);
    RUN_TEST(test_unsupported_command_is_nacked_in_the_sync_response);
    RUN_TEST(test_frame_for_another_device_gets_an_empty_sync_response);
    RUN_TEST(test_corrupted_frame_gets_an_empty_sync_response);
    RUN_TEST(test_status_of_an_empty_drive_is_still_answered);
    RUN_TEST(test_frame_sent_byte_by_byte_is_assembled);
    RUN_TEST(test_short_frame_is_ignored_and_released);
    RUN_TEST(test_strict_profile_does_not_answer_hub_framed_messages);
    RUN_TEST(test_cold_reset_discards_a_partial_frame);
    RUN_TEST(test_sync_without_frame_is_released_at_once);
    RUN_TEST(test_write_data_sync_is_released_empty);
    RUN_TEST(test_trailer_gaps_are_counted);
    RUN_TEST(test_without_credit_the_transport_asks_and_waits);
    RUN_TEST(test_reconnect_without_new_credit_asks_before_completing);
    RUN_TEST(test_partial_frame_does_not_survive_a_reconnect);
    RUN_TEST(test_credit_timeout_drops_the_transaction_and_the_next_one_succeeds);
    RUN_TEST(test_failed_ack_is_released_with_an_empty_sync_response);
    RUN_TEST(test_receive_error_is_a_transport_failure);
    RUN_TEST(test_answering_a_sync_with_more_than_one_byte_is_refused);
    RUN_TEST(test_command_bytes_carry_pc_clock_edges);
    return UNITY_END();
}
