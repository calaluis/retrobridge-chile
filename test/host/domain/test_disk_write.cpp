// Writing sectors (menu-escritura-disco, W002): DiskImage::writeSector,
// VirtualDrive::validateWrite/writeSector and write protection in the status.
// A disk is writable exactly when its byte source is (M45-1, M45-3).

#include <array>
#include <cstdint>
#include <utility>

#include "fakes/fake_byte_source.hpp"
#include "fakes/memory_byte_source.hpp"
#include "rb/domain/disk_image.hpp"
#include "rb/domain/virtual_drive.hpp"
#include "support/atr_header.hpp"
#include "unity.h"

using rb::core::ErrorCode;
using rb::core::Result;
using rb::core::Span;
using rb::domain::DiskImage;
using rb::domain::DriveNumber;
using rb::domain::SectorNumber;
using rb::domain::VirtualDrive;
using rb::test::dd_data_size;
using rb::test::FakeByteSource;
using rb::test::kAtrHeaderSize;
using rb::test::make_atr_header;
using rb::test::MemoryByteSource;
using rb::test::sd_data_size;

namespace {

MemoryByteSource sd_memory(bool writable)
{
    return MemoryByteSource{make_atr_header(sd_data_size(720), 128), kAtrHeaderSize + sd_data_size(720), writable};
}

MemoryByteSource dd_memory(bool writable)
{
    return MemoryByteSource{make_atr_header(dd_data_size(720), 256), kAtrHeaderSize + dd_data_size(720), writable};
}

DiskImage open_ok(rb::domain::ImageByteSource& source)
{
    Result<DiskImage> result = DiskImage::open(source);
    TEST_ASSERT_TRUE(result.ok());
    return std::move(result).value();
}

SectorNumber sector(uint16_t n)
{
    return SectorNumber::from(n).value();
}

std::array<uint8_t, 256> pattern(uint8_t seed)
{
    std::array<uint8_t, 256> data{};
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>(seed + i * 5u);
    }
    return data;
}

DriveNumber d2()
{
    return DriveNumber::from(2).value();
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_default_source_is_not_writable(void)
{
    FakeByteSource source{make_atr_header(sd_data_size(720), 128), kAtrHeaderSize + sd_data_size(720)};
    TEST_ASSERT_FALSE(source.writable());
    const std::array<uint8_t, 4> data{1, 2, 3, 4};
    const Result<void> written = source.writeAt(100, Span<const uint8_t>{data});
    TEST_ASSERT_FALSE(written.ok());
    TEST_ASSERT_EQUAL(static_cast<int>(ErrorCode::WriteProtected), static_cast<int>(written.error()));
}

static void test_read_only_follows_the_source(void)
{
    MemoryByteSource writable = sd_memory(true);
    MemoryByteSource locked = sd_memory(false);
    TEST_ASSERT_FALSE(open_ok(writable).readOnly());
    TEST_ASSERT_TRUE(open_ok(locked).readOnly());
}

static void test_writes_a_single_density_sector_and_reads_it_back(void)
{
    MemoryByteSource source = sd_memory(true);
    DiskImage image = open_ok(source);
    const std::array<uint8_t, 256> data = pattern(0x40);
    const Result<uint16_t> written = image.writeSector(sector(10), Span<const uint8_t>{data.data(), 128});
    TEST_ASSERT_TRUE(written.ok());
    TEST_ASSERT_EQUAL_UINT16(128, written.value());
    std::array<uint8_t, 256> back{};
    TEST_ASSERT_TRUE(image.readSector(sector(10), Span<uint8_t>{back}).ok());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data.data(), back.data(), 128);
    // The neighbours are untouched.
    const uint32_t at = kAtrHeaderSize + 9u * 128u;
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>((at - 1u) * 3u), source.bytes()[at - 1]);
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>((at + 128u) * 3u), source.bytes()[at + 128]);
}

static void test_double_density_boot_sectors_are_128_and_the_rest_256(void)
{
    MemoryByteSource source = dd_memory(true);
    DiskImage image = open_ok(source);
    const std::array<uint8_t, 256> boot = pattern(0x11);
    const std::array<uint8_t, 256> data = pattern(0x77);
    TEST_ASSERT_EQUAL_UINT16(128, image.writeSector(sector(3), Span<const uint8_t>{boot}).value());
    TEST_ASSERT_EQUAL_UINT16(256, image.writeSector(sector(4), Span<const uint8_t>{data}).value());
    // Sector 3 ends where sector 4 starts: 16 + 3 x 128.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(boot.data(), &source.bytes()[kAtrHeaderSize + 256u], 128);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data.data(), &source.bytes()[kAtrHeaderSize + 384u], 256);
}

static void test_short_buffer_is_refused_without_writing(void)
{
    MemoryByteSource source = dd_memory(true);
    DiskImage image = open_ok(source);
    const std::array<uint8_t, 256> data = pattern(1);
    const Result<uint16_t> written = image.writeSector(sector(4), Span<const uint8_t>{data.data(), 128});
    TEST_ASSERT_EQUAL(static_cast<int>(ErrorCode::BufferTooSmall), static_cast<int>(written.error()));
    TEST_ASSERT_EQUAL_UINT(0, source.writes());
}

static void test_sector_beyond_the_disk_is_refused(void)
{
    MemoryByteSource source = sd_memory(true);
    DiskImage image = open_ok(source);
    const std::array<uint8_t, 256> data = pattern(2);
    const Result<uint16_t> written = image.writeSector(sector(721), Span<const uint8_t>{data});
    TEST_ASSERT_EQUAL(static_cast<int>(ErrorCode::SectorOutOfRange), static_cast<int>(written.error()));
    TEST_ASSERT_EQUAL_UINT(0, source.writes());
}

static void test_protected_disk_refuses_and_keeps_its_content(void)
{
    MemoryByteSource source = sd_memory(false);
    const std::vector<uint8_t> before = source.bytes();
    DiskImage image = open_ok(source);
    const std::array<uint8_t, 256> data = pattern(3);
    const Result<uint16_t> written = image.writeSector(sector(10), Span<const uint8_t>{data});
    TEST_ASSERT_EQUAL(static_cast<int>(ErrorCode::WriteProtected), static_cast<int>(written.error()));
    TEST_ASSERT_TRUE(before == source.bytes());
}

static void test_moved_from_image_refuses_writes(void)
{
    MemoryByteSource source = sd_memory(true);
    DiskImage image = open_ok(source);
    DiskImage other = std::move(image);
    const std::array<uint8_t, 256> data = pattern(4);
    const Result<uint16_t> written = image.writeSector(sector(10), Span<const uint8_t>{data});  // NOLINT(bugprone-use-after-move)
    TEST_ASSERT_EQUAL(static_cast<int>(ErrorCode::InvalidDiskImage), static_cast<int>(written.error()));
    TEST_ASSERT_TRUE(other.isValid());
}

static void test_io_error_is_reported(void)
{
    MemoryByteSource source = sd_memory(true);
    source.fail_writes(true);
    DiskImage image = open_ok(source);
    const std::array<uint8_t, 256> data = pattern(5);
    const Result<uint16_t> written = image.writeSector(sector(10), Span<const uint8_t>{data});
    TEST_ASSERT_EQUAL(static_cast<int>(ErrorCode::IoError), static_cast<int>(written.error()));
}

static void test_drive_validates_writes_without_io(void)
{
    VirtualDrive drive{d2()};
    TEST_ASSERT_EQUAL(static_cast<int>(ErrorCode::NoDiskMounted),
                      static_cast<int>(drive.validateWrite(sector(1)).error()));
    MemoryByteSource source = dd_memory(false);  // protection is checked after the data frame, not here
    TEST_ASSERT_TRUE(drive.mount(open_ok(source)).ok());
    TEST_ASSERT_EQUAL_UINT16(128, drive.validateWrite(sector(1)).value());
    TEST_ASSERT_EQUAL_UINT16(256, drive.validateWrite(sector(720)).value());
    TEST_ASSERT_EQUAL(static_cast<int>(ErrorCode::SectorOutOfRange),
                      static_cast<int>(drive.validateWrite(sector(721)).error()));
    TEST_ASSERT_EQUAL_UINT(0, source.writes());
}

static void test_drive_status_reports_protection_of_the_disk(void)
{
    VirtualDrive drive{d2()};
    MemoryByteSource writable = sd_memory(true);
    TEST_ASSERT_TRUE(drive.mount(open_ok(writable)).ok());
    TEST_ASSERT_FALSE(drive.status().writeProtected());
    MemoryByteSource locked = sd_memory(false);
    TEST_ASSERT_TRUE(drive.mount(open_ok(locked)).ok());
    TEST_ASSERT_TRUE(drive.status().writeProtected());
}

static void test_drive_write_tracks_failure_like_a_read(void)
{
    VirtualDrive drive{d2()};
    const std::array<uint8_t, 256> data = pattern(6);
    TEST_ASSERT_FALSE(drive.writeSector(sector(1), Span<const uint8_t>{data}).ok());  // empty drive
    TEST_ASSERT_TRUE(drive.status().lastOperationFailed());

    MemoryByteSource locked = sd_memory(false);
    TEST_ASSERT_TRUE(drive.mount(open_ok(locked)).ok());
    TEST_ASSERT_FALSE(drive.writeSector(sector(1), Span<const uint8_t>{data}).ok());
    TEST_ASSERT_TRUE(drive.status().lastOperationFailed());

    MemoryByteSource writable = sd_memory(true);
    TEST_ASSERT_TRUE(drive.mount(open_ok(writable)).ok());
    TEST_ASSERT_TRUE(drive.writeSector(sector(1), Span<const uint8_t>{data}).ok());
    TEST_ASSERT_FALSE(drive.status().lastOperationFailed());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data.data(), &writable.bytes()[kAtrHeaderSize], 128);
}

static void test_rewriting_the_same_sector_is_idempotent(void)
{
    MemoryByteSource source = sd_memory(true);
    DiskImage image = open_ok(source);
    const std::array<uint8_t, 256> data = pattern(7);
    TEST_ASSERT_TRUE(image.writeSector(sector(20), Span<const uint8_t>{data}).ok());
    const std::vector<uint8_t> once = source.bytes();
    TEST_ASSERT_TRUE(image.writeSector(sector(20), Span<const uint8_t>{data}).ok());
    TEST_ASSERT_TRUE(once == source.bytes());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_source_is_not_writable);
    RUN_TEST(test_read_only_follows_the_source);
    RUN_TEST(test_writes_a_single_density_sector_and_reads_it_back);
    RUN_TEST(test_double_density_boot_sectors_are_128_and_the_rest_256);
    RUN_TEST(test_short_buffer_is_refused_without_writing);
    RUN_TEST(test_sector_beyond_the_disk_is_refused);
    RUN_TEST(test_protected_disk_refuses_and_keeps_its_content);
    RUN_TEST(test_moved_from_image_refuses_writes);
    RUN_TEST(test_io_error_is_reported);
    RUN_TEST(test_drive_validates_writes_without_io);
    RUN_TEST(test_drive_status_reports_protection_of_the_disk);
    RUN_TEST(test_drive_write_tracks_failure_like_a_read);
    RUN_TEST(test_rewriting_the_same_sector_is_idempotent);
    return UNITY_END();
}
