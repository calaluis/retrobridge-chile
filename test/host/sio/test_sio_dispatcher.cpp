// SioCommandDispatcher: two-phase handling (ADR-005). accept() decides
// Ignore / NAK / Accepted without I/O; execute() runs the use case and builds
// the completion code plus the data frame, checked byte by byte.

#include <array>
#include <cstdint>
#include <cstring>
#include <utility>

#include "fakes/fake_byte_source.hpp"
#include "rb/app/drive_bay.hpp"
#include "rb/sio/sio_checksum.hpp"
#include "rb/sio/sio_codes.hpp"
#include "rb/sio/sio_command_dispatcher.hpp"
#include "support/atr_header.hpp"
#include "unity.h"

using rb::app::DriveBay;
using rb::app::ImageLocation;
using rb::core::Span;
using rb::domain::DiskImage;
using rb::domain::DriveNumber;
using rb::sio::Acceptance;
using rb::sio::Completion;
using rb::sio::sio_checksum;
using rb::sio::SioCommandDispatcher;
using rb::test::dd_data_size;
using rb::test::FakeByteSource;
using rb::test::kAtrHeaderSize;
using rb::test::make_atr_header;
using rb::test::sd_data_size;
namespace code = rb::sio::code;

namespace {

constexpr uint8_t kD1 = 0x31;
constexpr uint8_t kRead = 0x52;
constexpr uint8_t kStatus = 0x53;

using Buffer = std::array<uint8_t, SioCommandDispatcher::kMaxFrameSize>;

std::array<uint8_t, 5> frame(uint8_t device, uint8_t command, uint8_t aux1 = 0, uint8_t aux2 = 0)
{
    std::array<uint8_t, 5> bytes{device, command, aux1, aux2, 0};
    bytes[4] = sio_checksum(Span<const uint8_t>{bytes.data(), 4});
    return bytes;
}

std::array<uint8_t, 5> read_frame(uint16_t sector)
{
    return frame(kD1, kRead, static_cast<uint8_t>(sector & 0xFF), static_cast<uint8_t>(sector >> 8));
}

FakeByteSource sd_source(uint32_t sectors = 720)
{
    return FakeByteSource{make_atr_header(sd_data_size(sectors), 128), kAtrHeaderSize + sd_data_size(sectors)};
}

FakeByteSource dd_source()
{
    return FakeByteSource{make_atr_header(dd_data_size(720), 256), kAtrHeaderSize + dd_data_size(720)};
}

void mount(DriveBay& bay, FakeByteSource& source)
{
    auto result = bay.mount(DriveNumber::from(1).value(), DiskImage::open(source).value(),
                            ImageLocation::from("test.atr").value());
    TEST_ASSERT_TRUE(result.ok());
}

Acceptance accept(SioCommandDispatcher& dispatcher, const std::array<uint8_t, 5>& bytes)
{
    return dispatcher.accept(Span<const uint8_t>{bytes});
}

// Runs a full accepted command and returns the completion; `out` holds the frame.
Completion run(SioCommandDispatcher& dispatcher, const std::array<uint8_t, 5>& bytes, Buffer& out)
{
    TEST_ASSERT_TRUE_MESSAGE(accept(dispatcher, bytes) == Acceptance::Accepted, "expected Accepted");
    return dispatcher.execute(Span<uint8_t>{out});
}

void assert_status(SioCommandDispatcher& dispatcher, const std::array<uint8_t, 5>& expected)
{
    Buffer out{};
    const Completion completion = run(dispatcher, frame(kD1, kStatus), out);
    TEST_ASSERT_EQUAL_HEX8(code::kComplete, completion.code);
    TEST_ASSERT_EQUAL_UINT16(5, completion.frameLength);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.data(), out.data(), expected.size());
}

void assert_sector_frame(const Buffer& out, uint32_t file_offset, uint16_t length)
{
    for (uint16_t i = 0; i < length; ++i) {
        TEST_ASSERT_EQUAL_HEX8(FakeByteSource::data_byte(file_offset + i), out[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(sio_checksum(Span<const uint8_t>{out.data(), length}), out[length]);
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- Phase 1: Ignore ----------------------------------------------------------------

static void test_bad_checksum_is_ignored(void)
{
    DriveBay bay{1};
    SioCommandDispatcher dispatcher{bay};
    auto bytes = frame(kD1, kStatus);
    bytes[4] ^= 0x01;
    TEST_ASSERT_TRUE(accept(dispatcher, bytes) == Acceptance::Ignore);
    TEST_ASSERT_FALSE(dispatcher.hasPendingCommand());
    assert_status(dispatcher, {0x00, 0x7F, 0xE0, 0x00, 0x60});  // D0 not set by an ignored frame
}

static void test_other_devices_are_ignored(void)
{
    DriveBay bay{1};
    SioCommandDispatcher dispatcher{bay};
    TEST_ASSERT_TRUE(accept(dispatcher, frame(0x40, kStatus)) == Acceptance::Ignore);  // printer
    TEST_ASSERT_TRUE(accept(dispatcher, frame(0x32, kStatus)) == Acceptance::Ignore);  // D2 not configured
    TEST_ASSERT_TRUE(accept(dispatcher, frame(0x30, kStatus)) == Acceptance::Ignore);
    TEST_ASSERT_TRUE(accept(dispatcher, frame(0x39, kStatus)) == Acceptance::Ignore);
    TEST_ASSERT_TRUE(accept(dispatcher, frame(0x32, 0x57)) == Acceptance::Ignore);  // never NAK for another ID
    TEST_ASSERT_FALSE(dispatcher.hasPendingCommand());
}

static void test_short_frame_is_ignored(void)
{
    DriveBay bay{1};
    SioCommandDispatcher dispatcher{bay};
    const std::array<uint8_t, 4> bytes{kD1, kStatus, 0x00, 0x00};
    TEST_ASSERT_TRUE(dispatcher.accept(Span<const uint8_t>{bytes}) == Acceptance::Ignore);
}

// --- Phase 1: NAK -------------------------------------------------------------------

static void test_unsupported_commands_are_nacked(void)
{
    DriveBay bay{1};
    SioCommandDispatcher dispatcher{bay};
    const uint8_t unsupported[] = {0x57, 0x50, 0x21, 0x4E, 0x4F, 0x3F, 0x00, 0xFF};
    for (uint8_t command : unsupported) {
        TEST_ASSERT_TRUE(accept(dispatcher, frame(kD1, command, 1, 0)) == Acceptance::Nak);
        TEST_ASSERT_FALSE(dispatcher.hasPendingCommand());
    }
}

static void test_invalid_sectors_are_nacked(void)
{
    FakeByteSource sd = sd_source();
    DriveBay bay{1};
    mount(bay, sd);
    SioCommandDispatcher dispatcher{bay};
    const unsigned reads_before = sd.reads();

    TEST_ASSERT_TRUE(accept(dispatcher, read_frame(0)) == Acceptance::Nak);
    TEST_ASSERT_TRUE(accept(dispatcher, read_frame(721)) == Acceptance::Nak);
    TEST_ASSERT_TRUE(accept(dispatcher, read_frame(0xFFFF)) == Acceptance::Nak);
    TEST_ASSERT_EQUAL_UINT(reads_before, sd.reads());  // decided without I/O

    Buffer out{};
    TEST_ASSERT_EQUAL_UINT16(129, run(dispatcher, read_frame(720), out).frameLength);
}

static void test_double_density_sector_range(void)
{
    FakeByteSource dd = dd_source();
    DriveBay bay{1};
    mount(bay, dd);
    SioCommandDispatcher dispatcher{bay};
    TEST_ASSERT_TRUE(accept(dispatcher, read_frame(721)) == Acceptance::Nak);
    TEST_ASSERT_TRUE(accept(dispatcher, read_frame(720)) == Acceptance::Accepted);
}

static void test_nak_is_reported_once_by_status_d0(void)
{
    FakeByteSource sd = sd_source();
    DriveBay bay{1};
    mount(bay, sd);
    SioCommandDispatcher dispatcher{bay};
    TEST_ASSERT_TRUE(accept(dispatcher, read_frame(0)) == Acceptance::Nak);
    assert_status(dispatcher, {0x01, 0xFF, 0xE0, 0x00, 0xE1});  // D0 [HIP]; byte 1 unchanged: not a drive failure
    assert_status(dispatcher, {0x00, 0xFF, 0xE0, 0x00, 0xE0});  // cleared after being reported

    TEST_ASSERT_TRUE(accept(dispatcher, frame(kD1, 0x57, 1, 0)) == Acceptance::Nak);
    Buffer out{};
    (void)run(dispatcher, read_frame(1), out);  // an accepted command also clears it
    assert_status(dispatcher, {0x00, 0xFF, 0xE0, 0x00, 0xE0});
}

// --- Separation of phases ------------------------------------------------------------

static void test_accept_does_no_io_and_execute_does(void)
{
    FakeByteSource sd = sd_source();
    DriveBay bay{1};
    mount(bay, sd);
    SioCommandDispatcher dispatcher{bay};

    const unsigned reads_before = sd.reads();
    TEST_ASSERT_TRUE(accept(dispatcher, read_frame(1)) == Acceptance::Accepted);
    TEST_ASSERT_TRUE(dispatcher.hasPendingCommand());
    TEST_ASSERT_EQUAL_UINT(reads_before, sd.reads());  // ACK can be sent before any read

    Buffer out{};
    (void)dispatcher.execute(Span<uint8_t>{out});
    TEST_ASSERT_EQUAL_UINT(reads_before + 1, sd.reads());
    TEST_ASSERT_FALSE(dispatcher.hasPendingCommand());
}

// --- Phase 2: READ --------------------------------------------------------------------

static void test_read_128_byte_sector(void)
{
    FakeByteSource sd = sd_source();
    DriveBay bay{1};
    mount(bay, sd);
    SioCommandDispatcher dispatcher{bay};
    Buffer out{};
    const Completion completion = run(dispatcher, read_frame(5), out);
    TEST_ASSERT_EQUAL_HEX8(code::kComplete, completion.code);
    TEST_ASSERT_EQUAL_UINT16(129, completion.frameLength);  // 128 data + checksum
    assert_sector_frame(out, kAtrHeaderSize + 4 * 128, 128);
}

static void test_read_double_density_sectors(void)
{
    FakeByteSource dd = dd_source();
    DriveBay bay{1};
    mount(bay, dd);
    SioCommandDispatcher dispatcher{bay};
    Buffer out{};

    Completion boot = run(dispatcher, read_frame(3), out);
    TEST_ASSERT_EQUAL_UINT16(129, boot.frameLength);
    assert_sector_frame(out, kAtrHeaderSize + 2 * 128, 128);

    Completion data = run(dispatcher, read_frame(4), out);
    TEST_ASSERT_EQUAL_HEX8(code::kComplete, data.code);
    TEST_ASSERT_EQUAL_UINT16(257, data.frameLength);  // 256 data + checksum
    assert_sector_frame(out, kAtrHeaderSize + 3 * 128, 256);
}

static void test_read_failure_sends_error_with_zeroed_frame(void)
{
    FakeByteSource dd = dd_source();
    DriveBay bay{1};
    mount(bay, dd);
    SioCommandDispatcher dispatcher{bay};
    dd.fail_reads(true);

    Buffer out{};
    out.fill(0xAA);
    const Completion completion = run(dispatcher, read_frame(10), out);
    TEST_ASSERT_EQUAL_HEX8(code::kError, completion.code);
    TEST_ASSERT_EQUAL_UINT16(257, completion.frameLength);  // still the expected size (ADR-005)
    for (uint16_t i = 0; i < 256; ++i) {
        TEST_ASSERT_EQUAL_HEX8(0x00, out[i]);
    }
    TEST_ASSERT_EQUAL_HEX8(0x00, out[256]);  // checksum of zeros

    dd.fail_reads(false);
    assert_status(dispatcher, {0x20, 0xEF, 0xE0, 0x00, 0xF0});  // $20+$EF=$10F->$10; +$E0=$F0
}

static void test_read_on_empty_drive_sends_error_with_128_zeros(void)
{
    DriveBay bay{1};
    SioCommandDispatcher dispatcher{bay};
    Buffer out{};
    out.fill(0xAA);
    const Completion completion = run(dispatcher, read_frame(1), out);
    TEST_ASSERT_EQUAL_HEX8(code::kError, completion.code);
    TEST_ASSERT_EQUAL_UINT16(129, completion.frameLength);
    for (uint16_t i = 0; i <= 128; ++i) {
        TEST_ASSERT_EQUAL_HEX8(0x00, out[i]);
    }
}

// --- Phase 2: STATUS --------------------------------------------------------------------

static void test_status_for_each_geometry(void)
{
    DriveBay empty{1};
    SioCommandDispatcher on_empty{empty};
    assert_status(on_empty, {0x00, 0x7F, 0xE0, 0x00, 0x60});

    FakeByteSource sd = sd_source();
    DriveBay with_sd{1};
    mount(with_sd, sd);
    SioCommandDispatcher on_sd{with_sd};
    assert_status(on_sd, {0x00, 0xFF, 0xE0, 0x00, 0xE0});

    FakeByteSource ed = sd_source(1040);
    DriveBay with_ed{1};
    mount(with_ed, ed);
    SioCommandDispatcher on_ed{with_ed};
    assert_status(on_ed, {0x80, 0xFF, 0xE0, 0x00, 0x61});  // $80+$FF=$17F->$80; +$E0=$160->$61

    FakeByteSource dd = dd_source();
    DriveBay with_dd{1};
    mount(with_dd, dd);
    SioCommandDispatcher on_dd{with_dd};
    assert_status(on_dd, {0x20, 0xFF, 0xE0, 0x00, 0x01});
}

static void test_status_ignores_aux_bytes(void)
{
    DriveBay bay{1};
    SioCommandDispatcher dispatcher{bay};
    TEST_ASSERT_TRUE(accept(dispatcher, frame(kD1, kStatus, 0xFF, 0x12)) == Acceptance::Accepted);
    Buffer out{};
    TEST_ASSERT_EQUAL_HEX8(code::kComplete, dispatcher.execute(Span<uint8_t>{out}).code);
}

// --- Recovery (H2c) -----------------------------------------------------------------------

static void test_abandon_discards_the_pending_command_without_io(void)
{
    FakeByteSource sd = sd_source();
    DriveBay bay{1};
    mount(bay, sd);
    SioCommandDispatcher dispatcher{bay};

    const unsigned reads_before = sd.reads();
    TEST_ASSERT_TRUE(accept(dispatcher, read_frame(1)) == Acceptance::Accepted);
    dispatcher.abandon();  // e.g. the ACK could not be transmitted
    TEST_ASSERT_FALSE(dispatcher.hasPendingCommand());
    TEST_ASSERT_EQUAL_UINT(reads_before, sd.reads());  // never executed

    TEST_ASSERT_TRUE(accept(dispatcher, read_frame(2)) == Acceptance::Accepted);  // ready for the next one
    dispatcher.abandon();
    dispatcher.abandon();  // no pending command: no effect
    assert_status(dispatcher, {0x00, 0xFF, 0xE0, 0x00, 0xE0});
}

// Death test: execute() without an accepted command is a programming error.
static int execute_without_accept(void)
{
    DriveBay bay{1};
    SioCommandDispatcher dispatcher{bay};
    Buffer out{};
    (void)dispatcher.execute(Span<uint8_t>{out});
    return 0;
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--execute-without-accept") == 0) {
        return execute_without_accept();
    }

    UNITY_BEGIN();
    RUN_TEST(test_bad_checksum_is_ignored);
    RUN_TEST(test_other_devices_are_ignored);
    RUN_TEST(test_short_frame_is_ignored);
    RUN_TEST(test_unsupported_commands_are_nacked);
    RUN_TEST(test_invalid_sectors_are_nacked);
    RUN_TEST(test_double_density_sector_range);
    RUN_TEST(test_nak_is_reported_once_by_status_d0);
    RUN_TEST(test_accept_does_no_io_and_execute_does);
    RUN_TEST(test_read_128_byte_sector);
    RUN_TEST(test_read_double_density_sectors);
    RUN_TEST(test_read_failure_sends_error_with_zeroed_frame);
    RUN_TEST(test_read_on_empty_drive_sends_error_with_128_zeros);
    RUN_TEST(test_status_for_each_geometry);
    RUN_TEST(test_status_ignores_aux_bytes);
    RUN_TEST(test_abandon_discards_the_pending_command_without_io);
    return UNITY_END();
}
