// Value objects: SectorNumber, DriveNumber, DiskGeometry, DriveStatus.

#include "rb/domain/disk_geometry.hpp"
#include "rb/domain/drive_number.hpp"
#include "rb/domain/drive_status.hpp"
#include "rb/domain/sector_number.hpp"
#include "unity.h"

using rb::core::ErrorCode;
using rb::domain::Density;
using rb::domain::DiskGeometry;
using rb::domain::DriveNumber;
using rb::domain::DriveStatus;
using rb::domain::SectorNumber;
using rb::domain::SectorSize;

namespace {

SectorNumber sector(uint16_t value)
{
    return SectorNumber::from(value).value();
}

DiskGeometry geometry(uint32_t count, SectorSize size)
{
    return DiskGeometry::create(count, size).value();
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- SectorNumber -----------------------------------------------------------

static void test_sector_number_rejects_zero(void)
{
    const auto result = SectorNumber::from(0);
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_TRUE(result.error() == ErrorCode::InvalidSectorNumber);
}

static void test_sector_number_accepts_full_16_bit_range(void)
{
    TEST_ASSERT_EQUAL_UINT16(1, sector(1).value());
    TEST_ASSERT_EQUAL_UINT16(65535, sector(65535).value());
}

static void test_sector_number_comparison(void)
{
    TEST_ASSERT_TRUE(sector(4) == sector(4));
    TEST_ASSERT_TRUE(sector(3) != sector(4));
}

// --- DriveNumber ------------------------------------------------------------

static void test_drive_number_range(void)
{
    TEST_ASSERT_TRUE(DriveNumber::from(0).error() == ErrorCode::InvalidDriveNumber);
    TEST_ASSERT_TRUE(DriveNumber::from(9).error() == ErrorCode::InvalidDriveNumber);
    TEST_ASSERT_EQUAL_UINT8(1, DriveNumber::from(1).value().value());
    TEST_ASSERT_EQUAL_UINT8(8, DriveNumber::from(8).value().value());
}

// --- DiskGeometry -----------------------------------------------------------

static void test_geometry_rejects_empty_and_oversized(void)
{
    TEST_ASSERT_TRUE(DiskGeometry::create(0, SectorSize::Bytes128).error() == ErrorCode::InvalidGeometry);
    TEST_ASSERT_TRUE(DiskGeometry::create(65536, SectorSize::Bytes256).error() == ErrorCode::InvalidGeometry);
    TEST_ASSERT_TRUE(DiskGeometry::create(1, SectorSize::Bytes128).ok());
    TEST_ASSERT_TRUE(DiskGeometry::create(65535, SectorSize::Bytes256).ok());
}

static void test_geometry_contains(void)
{
    const DiskGeometry sd = geometry(720, SectorSize::Bytes128);
    TEST_ASSERT_TRUE(sd.contains(sector(1)));
    TEST_ASSERT_TRUE(sd.contains(sector(720)));
    TEST_ASSERT_FALSE(sd.contains(sector(721)));
}

static void test_geometry_boot_sectors_are_128_bytes_in_double_density(void)
{
    const DiskGeometry dd = geometry(720, SectorSize::Bytes256);
    TEST_ASSERT_EQUAL_UINT16(128, dd.sizeOf(sector(1)));
    TEST_ASSERT_EQUAL_UINT16(128, dd.sizeOf(sector(3)));
    TEST_ASSERT_EQUAL_UINT16(256, dd.sizeOf(sector(4)));
    TEST_ASSERT_EQUAL_UINT16(256, dd.sizeOf(sector(720)));

    const DiskGeometry sd = geometry(720, SectorSize::Bytes128);
    TEST_ASSERT_EQUAL_UINT16(128, sd.sizeOf(sector(4)));
}

static void test_geometry_density(void)
{
    TEST_ASSERT_TRUE(geometry(720, SectorSize::Bytes128).density() == Density::Single);
    TEST_ASSERT_TRUE(geometry(1040, SectorSize::Bytes128).density() == Density::Enhanced);
    TEST_ASSERT_TRUE(geometry(720, SectorSize::Bytes256).density() == Density::Double);
    TEST_ASSERT_TRUE(geometry(1040, SectorSize::Bytes256).density() == Density::NonStandard);
    TEST_ASSERT_TRUE(geometry(100, SectorSize::Bytes128).density() == Density::NonStandard);
}

// --- DriveStatus ------------------------------------------------------------

static void test_drive_status_is_a_value(void)
{
    const DriveStatus a{true, true, false, Density::Single};
    const DriveStatus b{true, true, false, Density::Single};
    const DriveStatus c{true, true, true, Density::Single};
    TEST_ASSERT_TRUE(a == b);
    TEST_ASSERT_TRUE(a != c);
    TEST_ASSERT_TRUE(a.diskPresent());
    TEST_ASSERT_TRUE(a.writeProtected());
    TEST_ASSERT_FALSE(a.lastOperationFailed());
    TEST_ASSERT_TRUE(a.density() == Density::Single);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sector_number_rejects_zero);
    RUN_TEST(test_sector_number_accepts_full_16_bit_range);
    RUN_TEST(test_sector_number_comparison);
    RUN_TEST(test_drive_number_range);
    RUN_TEST(test_geometry_rejects_empty_and_oversized);
    RUN_TEST(test_geometry_contains);
    RUN_TEST(test_geometry_boot_sectors_are_128_bytes_in_double_density);
    RUN_TEST(test_geometry_density);
    RUN_TEST(test_drive_status_is_a_value);
    return UNITY_END();
}
