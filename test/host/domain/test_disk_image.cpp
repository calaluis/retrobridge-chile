// DiskImage: creation from an ImageByteSource, sector reads and the ownership
// policy (move-only, exclusive attachment of the source).

#include <array>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "fakes/fake_byte_source.hpp"
#include "rb/domain/disk_image.hpp"
#include "support/atr_header.hpp"
#include "unity.h"

using rb::core::ErrorCode;
using rb::core::Result;
using rb::core::Span;
using rb::domain::DiskImage;
using rb::domain::SectorNumber;
using rb::test::dd_data_size;
using rb::test::FakeByteSource;
using rb::test::kAtrHeaderSize;
using rb::test::make_atr_header;
using rb::test::sd_data_size;

// Ownership policy, enforced at compile time (H1-8, problem 3).
static_assert(!std::is_copy_constructible<DiskImage>::value, "DiskImage must not be copyable");
static_assert(!std::is_copy_assignable<DiskImage>::value, "DiskImage must not be copy-assignable");
static_assert(std::is_nothrow_move_constructible<DiskImage>::value, "DiskImage must be movable");
static_assert(std::is_nothrow_move_assignable<DiskImage>::value, "DiskImage must be move-assignable");
static_assert(!std::is_copy_constructible<Result<DiskImage>>::value, "Result<DiskImage> must not be copyable");
static_assert(std::is_move_constructible<Result<DiskImage>>::value, "Result<DiskImage> must be movable");
static_assert(!std::is_copy_constructible<rb::domain::ImageByteSource>::value, "sources must not be copyable");

namespace {

FakeByteSource sd_source(uint32_t sectors = 720)
{
    return FakeByteSource{make_atr_header(sd_data_size(sectors), 128), kAtrHeaderSize + sd_data_size(sectors)};
}

FakeByteSource dd_source(uint32_t sectors = 720)
{
    return FakeByteSource{make_atr_header(dd_data_size(sectors), 256), kAtrHeaderSize + dd_data_size(sectors)};
}

DiskImage open_ok(FakeByteSource& source)
{
    Result<DiskImage> result = DiskImage::open(source);
    TEST_ASSERT_TRUE_MESSAGE(result.ok(), "expected DiskImage::open to succeed");
    return std::move(result).value();
}

SectorNumber sector(uint16_t value)
{
    return SectorNumber::from(value).value();
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- Creation ----------------------------------------------------------------

static void test_open_valid_image_attaches_source(void)
{
    FakeByteSource source = sd_source();
    TEST_ASSERT_FALSE(source.isAttached());
    DiskImage image = open_ok(source);
    TEST_ASSERT_TRUE(image.isValid());
    TEST_ASSERT_TRUE(image.readOnly());
    TEST_ASSERT_EQUAL_UINT32(720, image.geometry().sectorCount());
    TEST_ASSERT_TRUE(source.isAttached());
    TEST_ASSERT_EQUAL_PTR(&source, image.source());
}

static void test_open_invalid_header_leaves_source_detached(void)
{
    auto header = make_atr_header(sd_data_size(720), 128);
    header[1] = 0x00;
    FakeByteSource source{header, kAtrHeaderSize + sd_data_size(720)};
    Result<DiskImage> result = DiskImage::open(source);
    TEST_ASSERT_TRUE(result.error() == rb::core::ErrorCode::InvalidAtrHeader);
    TEST_ASSERT_FALSE(source.isAttached());
}

static void test_open_source_shorter_than_header_never_reads_out_of_bounds(void)
{
    FakeByteSource source{make_atr_header(0, 128), 10};
    Result<DiskImage> result = DiskImage::open(source);
    TEST_ASSERT_TRUE(result.error() == ErrorCode::ImageTooSmall);
    TEST_ASSERT_EQUAL_UINT(0, source.out_of_bounds_reads());
    TEST_ASSERT_FALSE(source.isAttached());
}

static void test_open_propagates_io_error(void)
{
    FakeByteSource source = sd_source();
    source.fail_reads(true);
    Result<DiskImage> result = DiskImage::open(source);
    TEST_ASSERT_TRUE(result.error() == ErrorCode::IoError);
    TEST_ASSERT_FALSE(source.isAttached());
}

// --- Ownership (H1-8) --------------------------------------------------------

static void test_source_cannot_back_two_images(void)
{
    // Problem 1: the same source instance cannot be opened (and so mounted) twice.
    FakeByteSource source = sd_source();
    DiskImage first = open_ok(source);
    Result<DiskImage> second = DiskImage::open(source);
    TEST_ASSERT_TRUE(second.error() == ErrorCode::SourceInUse);
    TEST_ASSERT_TRUE(first.isValid());
}

static void test_move_transfers_the_attachment(void)
{
    // Problem 3: moving never duplicates the reference.
    FakeByteSource source = sd_source();
    DiskImage original = open_ok(source);
    DiskImage moved{std::move(original)};
    TEST_ASSERT_FALSE(original.isValid());  // NOLINT(bugprone-use-after-move): checking the moved-from state
    TEST_ASSERT_NULL(original.source());
    TEST_ASSERT_TRUE(moved.isValid());
    TEST_ASSERT_TRUE(source.isAttached());
}

static void test_destruction_detaches_the_source(void)
{
    // Problems 2 and 4: a source is detached only when no image references it.
    FakeByteSource source = sd_source();
    {
        DiskImage image = open_ok(source);
        TEST_ASSERT_TRUE(source.isAttached());
    }
    TEST_ASSERT_FALSE(source.isAttached());
    DiskImage reopened = open_ok(source);
    TEST_ASSERT_TRUE(reopened.isValid());
}

static void test_move_assignment_detaches_the_replaced_source(void)
{
    FakeByteSource first_source = sd_source();
    FakeByteSource second_source = dd_source();
    DiskImage image = open_ok(first_source);
    image = open_ok(second_source);
    TEST_ASSERT_FALSE(first_source.isAttached());
    TEST_ASSERT_TRUE(second_source.isAttached());
    TEST_ASSERT_EQUAL_PTR(&second_source, image.source());
}

static void test_moved_from_image_refuses_reads(void)
{
    FakeByteSource source = sd_source();
    DiskImage original = open_ok(source);
    DiskImage moved{std::move(original)};
    std::array<uint8_t, 128> buffer{};
    const unsigned reads_before = source.reads();
    Result<uint16_t> result = original.readSector(sector(1), Span<uint8_t>{buffer});  // NOLINT(bugprone-use-after-move)
    TEST_ASSERT_TRUE(result.error() == ErrorCode::InvalidDiskImage);
    TEST_ASSERT_EQUAL_UINT(reads_before, source.reads());
}

// --- Sector reads ------------------------------------------------------------

static void check_sector_content(const std::array<uint8_t, 256>& buffer, uint32_t offset, uint16_t length)
{
    for (uint16_t i = 0; i < length; ++i) {
        TEST_ASSERT_EQUAL_UINT8(FakeByteSource::data_byte(offset + i), buffer[i]);
    }
}

static void test_reads_single_density_sector(void)
{
    FakeByteSource source = sd_source();
    DiskImage image = open_ok(source);
    std::array<uint8_t, 256> buffer{};
    buffer.fill(0xEE);
    Result<uint16_t> result = image.readSector(sector(5), Span<uint8_t>{buffer});
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_UINT16(128, result.value());
    check_sector_content(buffer, kAtrHeaderSize + 4 * 128, 128);
    TEST_ASSERT_EQUAL_UINT8(0xEE, buffer[128]);  // nothing written past the sector
}

static void test_reads_double_density_boot_and_data_sectors(void)
{
    FakeByteSource source = dd_source();
    DiskImage image = open_ok(source);
    std::array<uint8_t, 256> buffer{};

    Result<uint16_t> boot = image.readSector(sector(3), Span<uint8_t>{buffer});
    TEST_ASSERT_EQUAL_UINT16(128, boot.value());
    check_sector_content(buffer, kAtrHeaderSize + 2 * 128, 128);

    Result<uint16_t> data = image.readSector(sector(720), Span<uint8_t>{buffer});
    TEST_ASSERT_EQUAL_UINT16(256, data.value());
    check_sector_content(buffer, kAtrHeaderSize + 3 * 128 + 716 * 256, 256);
    TEST_ASSERT_EQUAL_UINT(0, source.out_of_bounds_reads());
}

static void test_small_buffer_is_rejected_without_reading(void)
{
    FakeByteSource source = dd_source();
    DiskImage image = open_ok(source);
    std::array<uint8_t, 128> buffer{};
    const unsigned reads_before = source.reads();
    Result<uint16_t> result = image.readSector(sector(4), Span<uint8_t>{buffer});
    TEST_ASSERT_TRUE(result.error() == ErrorCode::BufferTooSmall);
    TEST_ASSERT_EQUAL_UINT(reads_before, source.reads());
}

static void test_sector_out_of_range_is_rejected_without_reading(void)
{
    FakeByteSource source = sd_source();
    DiskImage image = open_ok(source);
    std::array<uint8_t, 128> buffer{};
    const unsigned reads_before = source.reads();
    Result<uint16_t> result = image.readSector(sector(721), Span<uint8_t>{buffer});
    TEST_ASSERT_TRUE(result.error() == ErrorCode::SectorOutOfRange);
    TEST_ASSERT_EQUAL_UINT(reads_before, source.reads());
}

static void test_read_error_is_propagated(void)
{
    FakeByteSource source = sd_source();
    DiskImage image = open_ok(source);
    source.fail_reads(true);
    std::array<uint8_t, 128> buffer{};
    Result<uint16_t> result = image.readSector(sector(1), Span<uint8_t>{buffer});
    TEST_ASSERT_TRUE(result.error() == ErrorCode::IoError);
}

static void test_last_sector_of_largest_image_stays_in_bounds(void)
{
    FakeByteSource source = dd_source(65535);
    DiskImage image = open_ok(source);
    std::array<uint8_t, 256> buffer{};
    Result<uint16_t> result = image.readSector(sector(65535), Span<uint8_t>{buffer});
    TEST_ASSERT_EQUAL_UINT16(256, result.value());
    TEST_ASSERT_EQUAL_UINT(0, source.out_of_bounds_reads());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_open_valid_image_attaches_source);
    RUN_TEST(test_open_invalid_header_leaves_source_detached);
    RUN_TEST(test_open_source_shorter_than_header_never_reads_out_of_bounds);
    RUN_TEST(test_open_propagates_io_error);
    RUN_TEST(test_source_cannot_back_two_images);
    RUN_TEST(test_move_transfers_the_attachment);
    RUN_TEST(test_destruction_detaches_the_source);
    RUN_TEST(test_move_assignment_detaches_the_replaced_source);
    RUN_TEST(test_moved_from_image_refuses_reads);
    RUN_TEST(test_reads_single_density_sector);
    RUN_TEST(test_reads_double_density_boot_and_data_sectors);
    RUN_TEST(test_small_buffer_is_rejected_without_reading);
    RUN_TEST(test_sector_out_of_range_is_rejected_without_reading);
    RUN_TEST(test_read_error_is_propagated);
    RUN_TEST(test_last_sector_of_largest_image_stays_in_bounds);
    return UNITY_END();
}
