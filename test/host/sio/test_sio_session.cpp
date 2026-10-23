// SioSession: full bus transactions over a scripted transport and a simulated
// clock (ADR-005, ADR-014). Times are simulated: they verify the sequence and the
// budgets, not the electrical timing, which is measured in H5-H7.

#include <array>
#include <cstdint>
#include <optional>

#include "fakes/fake_byte_source.hpp"
#include "fakes/fake_remote_byte_source.hpp"
#include "fakes/memory_image_repository.hpp"
#include "rb/app/image_location.hpp"
#include "rb/app/usecases/mount_disk_image.hpp"
#include "rb/domain/disk_image.hpp"
#include "rb/sio/sio_codes.hpp"
#include "session_harness.hpp"
#include "support/atr_header.hpp"
#include "unity.h"

using rb::app::ImageLocation;
using rb::app::MountDiskImage;
using rb::core::Span;
using rb::domain::DiskImage;
using rb::domain::DriveNumber;
using rb::sio::SioSession;
using rb::sio::StepOutcome;
using rb::test::dd_data_size;
using rb::test::FakeByteSource;
using rb::test::FakeRemoteByteSource;
using rb::test::kAtrHeaderSize;
using rb::test::make_atr_header;
using rb::test::MemoryImage;
using rb::test::MemoryImageRepository;
using rb::test::read_frame;
using rb::test::ScriptedSioTransport;
using rb::test::sd_data_size;
using rb::test::SessionHarness;
using rb::test::sio_byte_time_us;
using rb::test::sio_frame;
using rb::test::status_frame;
namespace code = rb::sio::code;

namespace {

constexpr uint64_t kAckTargetUs = 1000;   // design target (spec allows 16 ms)
constexpr uint64_t kT5MinUs = 250;        // Atari C016555 §9

const std::array<uint8_t, 16> kSdHeader = make_atr_header(sd_data_size(720), 128);
const std::array<uint8_t, 16> kDdHeader = make_atr_header(dd_data_size(720), 256);

void mount(SessionHarness& h, rb::domain::ImageByteSource& source)
{
    auto result = h.bay.mount(DriveNumber::from(1).value(), DiskImage::open(source).value(),
                              ImageLocation::from("test.atr").value());
    TEST_ASSERT_TRUE(result.ok());
}

void assert_sent(const ScriptedSioTransport::Sent& sent, const uint8_t* expected, std::size_t length)
{
    TEST_ASSERT_EQUAL_size_t(length, sent.length);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, sent.bytes.data(), length);
}

void assert_ack_then_completion(const ScriptedSioTransport& t, uint8_t completion, std::size_t frame_length)
{
    TEST_ASSERT_EQUAL_size_t(2, t.sends());
    const uint8_t ack = code::kAck;
    assert_sent(t.sent(0), &ack, 1);
    TEST_ASSERT_EQUAL_HEX8(completion, t.sent(1).bytes[0]);
    TEST_ASSERT_EQUAL_size_t(1 + frame_length, t.sent(1).length);  // code + data frame in one send
}

void assert_sector_frame(const ScriptedSioTransport::Sent& sent, uint32_t file_offset, uint16_t length)
{
    for (uint16_t i = 0; i < length; ++i) {
        TEST_ASSERT_EQUAL_HEX8(FakeByteSource::data_byte(file_offset + i), sent.bytes[1 + i]);
    }
    TEST_ASSERT_EQUAL_HEX8(rb::sio::sio_checksum(Span<const uint8_t>{sent.bytes.data() + 1, length}),
                           sent.bytes[1 + length]);
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- Complete transactions, byte by byte ------------------------------------------------

static void test_status_transaction(void)
{
    SessionHarness h;
    h.transport.push_frame(status_frame());
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);

    assert_ack_then_completion(h.transport, code::kComplete, 5);
    const uint8_t expected[] = {code::kComplete, 0x00, 0x7F, 0xE0, 0x00, 0x60};  // empty D1
    assert_sent(h.transport.sent(1), expected, sizeof expected);
    TEST_ASSERT_EQUAL_UINT(1, h.transport.releases());
}

static void test_read_128_byte_sector_transaction(void)
{
    SessionHarness h;
    FakeByteSource sd{kSdHeader, kAtrHeaderSize + sd_data_size(720)};
    mount(h, sd);
    h.transport.push_frame(read_frame(720));
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);
    assert_ack_then_completion(h.transport, code::kComplete, 129);
    assert_sector_frame(h.transport.sent(1), kAtrHeaderSize + 719 * 128, 128);
}

static void test_read_256_byte_sector_transaction(void)
{
    SessionHarness h;
    FakeByteSource dd{kDdHeader, kAtrHeaderSize + dd_data_size(720)};
    mount(h, dd);
    h.transport.push_frame(read_frame(4));
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);
    assert_ack_then_completion(h.transport, code::kComplete, 257);
    assert_sector_frame(h.transport.sent(1), kAtrHeaderSize + 3 * 128, 256);
}

static void test_read_error_sends_error_and_zeroed_frame(void)
{
    SessionHarness h;
    FakeByteSource sd{kSdHeader, kAtrHeaderSize + sd_data_size(720)};
    mount(h, sd);
    sd.fail_reads(true);
    h.transport.push_frame(read_frame(1));
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);
    assert_ack_then_completion(h.transport, code::kError, 129);
    for (std::size_t i = 1; i <= 129; ++i) {
        TEST_ASSERT_EQUAL_HEX8(0x00, h.transport.sent(1).bytes[i]);  // 128 zeros + checksum $00
    }
}

// --- Ignore and NAK ---------------------------------------------------------------------

static void test_ignored_frames_transmit_nothing(void)
{
    SessionHarness h;
    auto corrupted = status_frame();
    corrupted[4] ^= 0xFF;
    h.transport.push_frame(corrupted);
    h.transport.push_frame(sio_frame(0x40, rb::test::kStatusCommand));  // addressed to the printer
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Ignored);
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Ignored);
    TEST_ASSERT_EQUAL_size_t(0, h.transport.sends());
    TEST_ASSERT_EQUAL_UINT(2, h.transport.releases());
}

static void test_incomplete_and_overlong_frames_are_ignored(void)
{
    SessionHarness h;
    h.transport.push_frame(status_frame(), 3);
    h.transport.push_frame(status_frame(), 6);
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Ignored);
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Ignored);
    TEST_ASSERT_EQUAL_size_t(0, h.transport.sends());
    TEST_ASSERT_FALSE(h.dispatcher.hasPendingCommand());
}

static void test_nak_is_the_only_transmission(void)
{
    SessionHarness h;
    FakeByteSource sd{kSdHeader, kAtrHeaderSize + sd_data_size(720)};
    mount(h, sd);
    const unsigned reads_before = sd.reads();  // mounting read the header
    h.transport.push_frame(read_frame(0));
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Nacked);
    TEST_ASSERT_EQUAL_size_t(1, h.transport.sends());
    const uint8_t nak = code::kNak;
    assert_sent(h.transport.sent(0), &nak, 1);
    TEST_ASSERT_LESS_OR_EQUAL_UINT64(kAckTargetUs, h.transport.sent(0).startUs - h.transport.last_command_released_at());
    TEST_ASSERT_EQUAL_UINT(reads_before, sd.reads());
    TEST_ASSERT_EQUAL_UINT(1, h.transport.releases());
}

// --- Timing (simulated) ----------------------------------------------------------------

static void test_ack_within_target_and_completion_after_t5(void)
{
    SessionHarness h;
    FakeByteSource sd{kSdHeader, kAtrHeaderSize + sd_data_size(720)};
    mount(h, sd);
    h.transport.push_frame(read_frame(1));
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);

    const auto& ack = h.transport.sent(0);
    const auto& completion = h.transport.sent(1);
    TEST_ASSERT_LESS_OR_EQUAL_UINT64(kAckTargetUs, ack.startUs - h.transport.last_command_released_at());
    TEST_ASSERT_EQUAL_UINT64(sio_byte_time_us(1), ack.endUs - ack.startUs);  // send() ends after the last bit
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(kT5MinUs, completion.startUs - ack.endUs);
    TEST_ASSERT_EQUAL_UINT64(SioSession::kCompleteDelayUs, completion.startUs - ack.endUs);
}

static void test_remote_read_happens_after_the_ack(void)
{
    SessionHarness h;
    FakeRemoteByteSource remote{kSdHeader, kAtrHeaderSize + sd_data_size(720), h.clock, 0};
    mount(h, remote);              // header read without latency
    remote.set_latency(50000);     // 50 ms per sector read from now on
    const unsigned reads_before = remote.reads();

    h.transport.push_frame(read_frame(1));
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);
    TEST_ASSERT_EQUAL_UINT(reads_before + 1, remote.reads());

    const auto& ack = h.transport.sent(0);
    const auto& completion = h.transport.sent(1);
    TEST_ASSERT_LESS_OR_EQUAL_UINT64(kAckTargetUs, ack.startUs - h.transport.last_command_released_at());
    TEST_ASSERT_LESS_OR_EQUAL_UINT64(remote.last_read_started_at(), ack.endUs);  // ACK fully sent before the read
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(remote.last_read_started_at() + 50000, completion.startUs);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(kT5MinUs, completion.startUs - ack.endUs);
    TEST_ASSERT_EQUAL_HEX8(code::kComplete, completion.bytes[0]);
}

// --- Mailbox between transactions (ADR-014) -------------------------------------------------

struct MidReadMount {
    MountDiskImage* mount;
    bool submitted;
};

void submit_mount_during_read(void* context)
{
    auto* state = static_cast<MidReadMount*>(context);
    if (!state->submitted) {
        state->submitted = state->mount->execute(DriveNumber::from(1).value(), ImageLocation::from("dd.atr").value()).ok();
    }
}

static void test_mailbox_request_waits_for_the_transaction_to_end(void)
{
    SessionHarness h;
    MemoryImageRepository<2> repository;
    repository.add(MemoryImage{"dd.atr", kDdHeader, kAtrHeaderSize + dd_data_size(720)});
    MountDiskImage mount_image{repository, h.mailbox};

    FakeRemoteByteSource remote{kSdHeader, kAtrHeaderSize + sd_data_size(720), h.clock, 0};
    mount(h, remote);
    MidReadMount state{&mount_image, false};
    remote.set_hook(submit_mount_during_read, &state);  // storage context submits mid-transaction

    h.transport.push_frame(read_frame(1));
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);
    TEST_ASSERT_TRUE(state.submitted);
    TEST_ASSERT_EQUAL_HEX8(code::kComplete, h.transport.sent(1).bytes[0]);  // served from the SD image
    TEST_ASSERT_EQUAL_size_t(1 + 129, h.transport.sent(1).length);
    TEST_ASSERT_TRUE(remote.isAttached());  // not applied during the transaction

    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::MailboxApplied);  // applied between transactions
    TEST_ASSERT_FALSE(remote.isAttached());

    // The displaced source travels back, detached, for the storage context to
    // close. It was mounted by the test (not the repository), so it is checked
    // here instead of going through CollectDriveReply.
    std::optional<rb::app::DriveReply> reply = h.mailbox.takeReply();
    TEST_ASSERT_TRUE(reply.has_value());
    TEST_ASSERT_FALSE(reply->error.has_value());
    TEST_ASSERT_EQUAL_PTR(&remote, reply->displaced);
    TEST_ASSERT_EQUAL_UINT(1, repository.open_count());  // dd.atr, now mounted
}

static void test_pending_request_is_applied_before_waiting_for_the_bus(void)
{
    SessionHarness h;
    MemoryImageRepository<1> repository;
    repository.add(MemoryImage{"dd.atr", kDdHeader, kAtrHeaderSize + dd_data_size(720)});
    MountDiskImage mount_image{repository, h.mailbox};
    TEST_ASSERT_TRUE(mount_image.execute(DriveNumber::from(1).value(), ImageLocation::from("dd.atr").value()).ok());

    const uint64_t before = h.clock.nowMicros();
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::MailboxApplied);
    TEST_ASSERT_EQUAL_UINT(0, h.transport.waits());           // did not block on the bus
    TEST_ASSERT_EQUAL_UINT64(before, h.clock.nowMicros());    // O(1), no simulated time
    TEST_ASSERT_TRUE(h.bay.find(DriveNumber::from(1).value())->hasDisk());

    h.transport.push_frame(status_frame());
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);
    TEST_ASSERT_EQUAL_HEX8(0x20, h.transport.sent(1).bytes[1]);  // D5: the DD image is now mounted
}

static void test_idle_bus_times_out_quietly(void)
{
    SessionHarness h;
    const uint64_t before = h.clock.nowMicros();
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Idle);
    TEST_ASSERT_EQUAL_UINT64(before + SioSession::kPollTimeoutUs, h.clock.nowMicros());
    TEST_ASSERT_EQUAL_size_t(0, h.transport.sends());
    TEST_ASSERT_EQUAL_UINT(0, h.transport.releases());
}

// --- Transport errors and recovery ------------------------------------------------------------

static void test_failed_ack_abandons_the_command(void)
{
    SessionHarness h;
    FakeByteSource sd{kSdHeader, kAtrHeaderSize + sd_data_size(720)};
    mount(h, sd);
    const unsigned reads_before = sd.reads();  // mounting read the header
    h.transport.fail_send(0);
    h.transport.push_frame(read_frame(1));
    h.transport.push_frame(read_frame(2));

    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::TransportFailed);
    TEST_ASSERT_FALSE(h.dispatcher.hasPendingCommand());
    TEST_ASSERT_EQUAL_UINT(reads_before, sd.reads());  // never executed without an ACK
    TEST_ASSERT_EQUAL_UINT(1, h.transport.releases());

    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);  // the next command works
    assert_ack_then_completion(h.transport, code::kComplete, 129);
    TEST_ASSERT_TRUE(sd.isAttached());  // ownership untouched
}

static void test_failed_completion_releases_the_line_and_recovers(void)
{
    SessionHarness h;
    FakeByteSource sd{kSdHeader, kAtrHeaderSize + sd_data_size(720)};
    mount(h, sd);
    h.transport.fail_send(1);  // ACK succeeds, COMPLETE + data fails
    h.transport.push_frame(read_frame(1));
    h.transport.push_frame(status_frame());

    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::TransportFailed);
    TEST_ASSERT_FALSE(h.dispatcher.hasPendingCommand());
    TEST_ASSERT_EQUAL_UINT(1, h.transport.releases());

    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);
    TEST_ASSERT_EQUAL_HEX8(code::kComplete, h.transport.sent(2).bytes[0]);
    TEST_ASSERT_TRUE(sd.isAttached());
}

static void test_failed_nak_releases_the_line(void)
{
    SessionHarness h;
    h.transport.fail_send(0);
    h.transport.push_frame(sio_frame(rb::test::kD1, 0x57, 1, 0));
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::TransportFailed);
    TEST_ASSERT_EQUAL_UINT(1, h.transport.releases());
    TEST_ASSERT_FALSE(h.dispatcher.hasPendingCommand());
}

static void test_receive_error_releases_the_line(void)
{
    SessionHarness h;
    h.transport.push_receive_error();
    h.transport.push_frame(status_frame());
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::TransportFailed);
    TEST_ASSERT_EQUAL_size_t(0, h.transport.sends());
    TEST_ASSERT_EQUAL_UINT(1, h.transport.releases());
    TEST_ASSERT_TRUE(h.session.step() == StepOutcome::Completed);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_status_transaction);
    RUN_TEST(test_read_128_byte_sector_transaction);
    RUN_TEST(test_read_256_byte_sector_transaction);
    RUN_TEST(test_read_error_sends_error_and_zeroed_frame);
    RUN_TEST(test_ignored_frames_transmit_nothing);
    RUN_TEST(test_incomplete_and_overlong_frames_are_ignored);
    RUN_TEST(test_nak_is_the_only_transmission);
    RUN_TEST(test_ack_within_target_and_completion_after_t5);
    RUN_TEST(test_remote_read_happens_after_the_ack);
    RUN_TEST(test_mailbox_request_waits_for_the_transaction_to_end);
    RUN_TEST(test_pending_request_is_applied_before_waiting_for_the_bus);
    RUN_TEST(test_idle_bus_times_out_quietly);
    RUN_TEST(test_failed_ack_abandons_the_command);
    RUN_TEST(test_failed_completion_releases_the_line_and_recovers);
    RUN_TEST(test_failed_nak_releases_the_line);
    RUN_TEST(test_receive_error_releases_the_line);
    return UNITY_END();
}
