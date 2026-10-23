// AtrLayout: header validation, sector count from the real size (D-8),
// rejection of 256-byte boot sectors (D-6), offsets and bounds.

#include <array>
#include <cstdint>

#include "rb/domain/atr_layout.hpp"
#include "support/atr_header.hpp"
#include "unity.h"

using rb::core::ErrorCode;
using rb::core::Span;
using rb::domain::AtrLayout;
using rb::domain::ByteRange;
using rb::domain::Density;
using rb::domain::SectorNumber;
using rb::domain::SectorSize;
using rb::test::dd_data_size;
using rb::test::kAtrHeaderSize;
using rb::test::make_atr_header;
using rb::test::make_atr_header_paragraphs;
using rb::test::sd_data_size;

namespace {

rb::core::Result<AtrLayout> parse(const std::array<uint8_t, 16>& header, uint32_t data_size)
{
    return AtrLayout::parse(Span<const uint8_t>{header}, kAtrHeaderSize + data_size);
}

AtrLayout parse_ok(uint32_t data_size, uint16_t sector_size)
{
    auto result = parse(make_atr_header(data_size, sector_size), data_size);
    TEST_ASSERT_TRUE_MESSAGE(result.ok(), "expected a valid ATR layout");
    return result.value();
}

ErrorCode parse_error(const std::array<uint8_t, 16>& header, uint32_t data_size)
{
    auto result = parse(header, data_size);
    TEST_ASSERT_FALSE_MESSAGE(result.ok(), "expected the ATR layout to be rejected");
    return result.error();
}

ByteRange locate(const AtrLayout& layout, uint16_t sector)
{
    auto result = layout.locate(SectorNumber::from(sector).value());
    TEST_ASSERT_TRUE(result.ok());
    return result.value();
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- Header validation ------------------------------------------------------

static void test_rejects_short_header(void)
{
    const std::array<uint8_t, 15> short_header{0x96, 0x02};
    auto result = AtrLayout::parse(Span<const uint8_t>{short_header}, 16 + 128);
    TEST_ASSERT_TRUE(result.error() == ErrorCode::InvalidAtrHeader);
}

static void test_rejects_bad_magic(void)
{
    auto header = make_atr_header(sd_data_size(720), 128);
    header[0] = 0x97;
    TEST_ASSERT_TRUE(parse_error(header, sd_data_size(720)) == ErrorCode::InvalidAtrHeader);
}

static void test_rejects_unsupported_sector_sizes(void)
{
    TEST_ASSERT_TRUE(parse_error(make_atr_header(512 * 10, 512), 512 * 10) == ErrorCode::UnsupportedSectorSize);
    TEST_ASSERT_TRUE(parse_error(make_atr_header(1280, 0), 1280) == ErrorCode::UnsupportedSectorSize);
}

static void test_rejects_file_shorter_than_header(void)
{
    const auto header = make_atr_header(0, 128);
    auto result = AtrLayout::parse(Span<const uint8_t>{header}, 10);
    TEST_ASSERT_TRUE(result.error() == ErrorCode::ImageTooSmall);
}

static void test_rejects_image_without_a_full_sector(void)
{
    TEST_ASSERT_TRUE(parse_error(make_atr_header(0, 128), 0) == ErrorCode::ImageTooSmall);
    TEST_ASSERT_TRUE(parse_error(make_atr_header(127, 128), 127) == ErrorCode::ImageTooSmall);
    TEST_ASSERT_TRUE(parse_error(make_atr_header(100, 256), 100) == ErrorCode::ImageTooSmall);
}

// --- Standard layouts -------------------------------------------------------

static void test_accepts_single_density(void)
{
    const AtrLayout layout = parse_ok(sd_data_size(720), 128);
    TEST_ASSERT_EQUAL_UINT32(720, layout.geometry().sectorCount());
    TEST_ASSERT_TRUE(layout.geometry().density() == Density::Single);
    TEST_ASSERT_FALSE(layout.headerSizeMismatch());
    TEST_ASSERT_EQUAL_UINT32(0, layout.ignoredTrailingBytes());
}

static void test_accepts_enhanced_density(void)
{
    const AtrLayout layout = parse_ok(sd_data_size(1040), 128);
    TEST_ASSERT_TRUE(layout.geometry().density() == Density::Enhanced);
}

static void test_accepts_standard_double_density(void)
{
    const AtrLayout layout = parse_ok(dd_data_size(720), 256);
    TEST_ASSERT_EQUAL_UINT32(720, layout.geometry().sectorCount());
    TEST_ASSERT_TRUE(layout.geometry().sectorSize() == SectorSize::Bytes256);
    TEST_ASSERT_TRUE(layout.geometry().density() == Density::Double);
    TEST_ASSERT_EQUAL_UINT32(0, layout.ignoredTrailingBytes());
}

static void test_accepts_double_density_with_only_boot_sectors(void)
{
    // Fewer than four sectors: all of them are 128-byte boot sectors.
    const AtrLayout layout = parse_ok(dd_data_size(3), 256);
    TEST_ASSERT_EQUAL_UINT32(3, layout.geometry().sectorCount());
}

// --- D-6: 256-byte boot sectors are rejected ---------------------------------

static void test_rejects_double_density_with_256_byte_boot_sectors(void)
{
    // 720 sectors of 256 bytes, boot sectors included: data size is a multiple of 256.
    const uint32_t full_boot = 720 * 256;
    TEST_ASSERT_TRUE(parse_error(make_atr_header(full_boot, 256), full_boot) == ErrorCode::UnsupportedAtrLayout);

    const uint32_t small_full_boot = 4 * 256;
    TEST_ASSERT_TRUE(parse_error(make_atr_header(small_full_boot, 256), small_full_boot) ==
                     ErrorCode::UnsupportedAtrLayout);
}

// --- D-8: the real size prevails ---------------------------------------------

static void test_truncated_image_uses_real_size(void)
{
    // Header declares 720 sectors; the file only holds 700.
    const auto header = make_atr_header(sd_data_size(720), 128);
    auto result = parse(header, sd_data_size(700));
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_UINT32(700, result.value().geometry().sectorCount());
    TEST_ASSERT_TRUE(result.value().headerSizeMismatch());
}

static void test_header_smaller_than_file_uses_real_size(void)
{
    const auto header = make_atr_header(sd_data_size(700), 128);
    auto result = parse(header, sd_data_size(720));
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_UINT32(720, result.value().geometry().sectorCount());
    TEST_ASSERT_TRUE(result.value().headerSizeMismatch());
}

static void test_partial_trailing_sector_is_ignored_and_reported(void)
{
    const uint32_t sd = sd_data_size(720) + 50;
    auto sd_result = parse(make_atr_header(sd, 128), sd);
    TEST_ASSERT_TRUE(sd_result.ok());
    TEST_ASSERT_EQUAL_UINT32(720, sd_result.value().geometry().sectorCount());
    TEST_ASSERT_EQUAL_UINT32(50, sd_result.value().ignoredTrailingBytes());

    const uint32_t dd = dd_data_size(720) + 200;
    auto dd_result = parse(make_atr_header(dd, 256), dd);
    TEST_ASSERT_TRUE(dd_result.ok());
    TEST_ASSERT_EQUAL_UINT32(720, dd_result.value().geometry().sectorCount());
    TEST_ASSERT_EQUAL_UINT32(200, dd_result.value().ignoredTrailingBytes());
}

static void test_paragraph_high_byte_is_decoded(void)
{
    // 65535 SD sectors = 8 388 480 bytes = 524 280 paragraphs (needs the high byte).
    const uint32_t data = sd_data_size(65535);
    const auto header = make_atr_header_paragraphs(data / 16, 128);
    TEST_ASSERT_EQUAL_UINT8(0x07, header[6]);
    auto result = parse(header, data);
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_FALSE(result.value().headerSizeMismatch());
}

// --- Limits (H1-5) -----------------------------------------------------------

static void test_maximum_sector_count(void)
{
    const AtrLayout layout = parse_ok(dd_data_size(65535), 256);
    TEST_ASSERT_EQUAL_UINT32(65535, layout.geometry().sectorCount());
    const ByteRange last = locate(layout, 65535);
    TEST_ASSERT_EQUAL_UINT32(kAtrHeaderSize + dd_data_size(65535), last.offset + last.length);
}

static void test_rejects_more_than_65535_sectors(void)
{
    TEST_ASSERT_TRUE(parse_error(make_atr_header(sd_data_size(65536), 128), sd_data_size(65536)) ==
                     ErrorCode::ImageTooLarge);
    TEST_ASSERT_TRUE(parse_error(make_atr_header(dd_data_size(65536), 256), dd_data_size(65536)) ==
                     ErrorCode::ImageTooLarge);
}

static void test_huge_file_sizes_do_not_overflow(void)
{
    const auto header = make_atr_header_paragraphs(0xFFFFFF, 128);
    auto result = AtrLayout::parse(Span<const uint8_t>{header}, 0xFFFFFFFFu);
    TEST_ASSERT_TRUE(result.error() == ErrorCode::ImageTooLarge);
}

// --- Offsets -----------------------------------------------------------------

static void test_single_density_offsets(void)
{
    const AtrLayout layout = parse_ok(sd_data_size(720), 128);
    TEST_ASSERT_EQUAL_UINT32(16, locate(layout, 1).offset);
    TEST_ASSERT_EQUAL_UINT16(128, locate(layout, 1).length);
    TEST_ASSERT_EQUAL_UINT32(16 + 3 * 128, locate(layout, 4).offset);
    TEST_ASSERT_EQUAL_UINT32(16 + 719 * 128, locate(layout, 720).offset);
}

static void test_double_density_offsets(void)
{
    const AtrLayout layout = parse_ok(dd_data_size(720), 256);
    TEST_ASSERT_EQUAL_UINT32(16, locate(layout, 1).offset);
    TEST_ASSERT_EQUAL_UINT32(16 + 2 * 128, locate(layout, 3).offset);
    TEST_ASSERT_EQUAL_UINT16(128, locate(layout, 3).length);
    TEST_ASSERT_EQUAL_UINT32(16 + 3 * 128, locate(layout, 4).offset);
    TEST_ASSERT_EQUAL_UINT16(256, locate(layout, 4).length);
    TEST_ASSERT_EQUAL_UINT32(16 + 3 * 128 + 256, locate(layout, 5).offset);
}

static void test_sector_beyond_geometry_is_out_of_range(void)
{
    const AtrLayout layout = parse_ok(sd_data_size(720), 128);
    auto result = layout.locate(SectorNumber::from(721).value());
    TEST_ASSERT_TRUE(result.error() == ErrorCode::SectorOutOfRange);
}

static void check_all_sectors_within_file(uint32_t data_size, uint16_t sector_size)
{
    const uint32_t file_size = kAtrHeaderSize + data_size;
    const AtrLayout layout = parse_ok(data_size, sector_size);
    uint32_t expected_offset = kAtrHeaderSize;
    for (uint32_t n = 1; n <= layout.geometry().sectorCount(); ++n) {
        const ByteRange range = locate(layout, static_cast<uint16_t>(n));
        TEST_ASSERT_EQUAL_UINT32(expected_offset, range.offset);  // contiguous, no gaps or overlaps
        TEST_ASSERT_TRUE(range.offset + range.length <= file_size);
        expected_offset += range.length;
    }
}

static void test_every_sector_lies_within_the_file(void)
{
    check_all_sectors_within_file(sd_data_size(720), 128);
    check_all_sectors_within_file(sd_data_size(1040), 128);
    check_all_sectors_within_file(dd_data_size(720), 256);
    check_all_sectors_within_file(dd_data_size(720) + 77, 256);  // partial trailing sector
    check_all_sectors_within_file(dd_data_size(2), 256);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rejects_short_header);
    RUN_TEST(test_rejects_bad_magic);
    RUN_TEST(test_rejects_unsupported_sector_sizes);
    RUN_TEST(test_rejects_file_shorter_than_header);
    RUN_TEST(test_rejects_image_without_a_full_sector);
    RUN_TEST(test_accepts_single_density);
    RUN_TEST(test_accepts_enhanced_density);
    RUN_TEST(test_accepts_standard_double_density);
    RUN_TEST(test_accepts_double_density_with_only_boot_sectors);
    RUN_TEST(test_rejects_double_density_with_256_byte_boot_sectors);
    RUN_TEST(test_truncated_image_uses_real_size);
    RUN_TEST(test_header_smaller_than_file_uses_real_size);
    RUN_TEST(test_partial_trailing_sector_is_ignored_and_reported);
    RUN_TEST(test_paragraph_high_byte_is_decoded);
    RUN_TEST(test_maximum_sector_count);
    RUN_TEST(test_rejects_more_than_65535_sectors);
    RUN_TEST(test_huge_file_sizes_do_not_overflow);
    RUN_TEST(test_single_density_offsets);
    RUN_TEST(test_double_density_offsets);
    RUN_TEST(test_sector_beyond_geometry_is_out_of_range);
    RUN_TEST(test_every_sector_lies_within_the_file);
    return UNITY_END();
}
