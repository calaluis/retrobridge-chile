// VirtualDrive: empty/with-disk states, status, failure tracking and the
// mount/unmount ownership rules (H1-8).

#include <array>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "fakes/fake_byte_source.hpp"
#include "rb/domain/virtual_drive.hpp"
#include "support/atr_header.hpp"
#include "unity.h"

using rb::core::ErrorCode;
using rb::core::Result;
using rb::core::Span;
using rb::domain::Density;
using rb::domain::DiskImage;
using rb::domain::DriveNumber;
using rb::domain::DriveStatus;
using rb::domain::ImageByteSource;
using rb::domain::SectorNumber;
using rb::domain::VirtualDrive;
using rb::test::dd_data_size;
using rb::test::FakeByteSource;
using rb::test::kAtrHeaderSize;
using rb::test::make_atr_header;
using rb::test::sd_data_size;

// A drive is an entity with identity: it is neither copied nor moved.
static_assert(!std::is_copy_constructible<VirtualDrive>::value, "VirtualDrive must not be copyable");
static_assert(!std::is_move_constructible<VirtualDrive>::value, "VirtualDrive must not be movable");

namespace {

FakeByteSource sd_source()
{
    return FakeByteSource{make_atr_header(sd_data_size(720), 128), kAtrHeaderSize + sd_data_size(720)};
}

FakeByteSource dd_source()
{
    return FakeByteSource{make_atr_header(dd_data_size(720), 256), kAtrHeaderSize + dd_data_size(720)};
}

DiskImage open_ok(FakeByteSource& source)
{
    Result<DiskImage> result = DiskImage::open(source);
    TEST_ASSERT_TRUE(result.ok());
    return std::move(result).value();
}

VirtualDrive make_d1()
{
    return VirtualDrive{DriveNumber::from(1).value()};
}

SectorNumber sector(uint16_t value)
{
    return SectorNumber::from(value).value();
}

ImageByteSource* mount_ok(VirtualDrive& drive, DiskImage&& image)
{
    Result<ImageByteSource*> result = drive.mount(std::move(image));
    TEST_ASSERT_TRUE(result.ok());
    return result.value();
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- Empty drive --------------------------------------------------------------

static void test_new_drive_is_empty(void)
{
    VirtualDrive drive = make_d1();
    TEST_ASSERT_EQUAL_UINT8(1, drive.number().value());
    TEST_ASSERT_FALSE(drive.hasDisk());
    TEST_ASSERT_TRUE(drive.status() == (DriveStatus{false, true, false, Density::Unknown}));
}

static void test_read_on_empty_drive_fails_and_is_reported(void)
{
    VirtualDrive drive = make_d1();
    std::array<uint8_t, 128> buffer{};
    Result<uint16_t> result = drive.readSector(sector(1), Span<uint8_t>{buffer});
    TEST_ASSERT_TRUE(result.error() == ErrorCode::NoDiskMounted);
    TEST_ASSERT_TRUE(drive.status().lastOperationFailed());
}

// --- Mount / read ---------------------------------------------------------------

static void test_mount_reports_disk_and_density(void)
{
    FakeByteSource source = dd_source();
    VirtualDrive drive = make_d1();
    TEST_ASSERT_NULL(mount_ok(drive, open_ok(source)));  // nothing displaced
    TEST_ASSERT_TRUE(drive.hasDisk());
    TEST_ASSERT_TRUE(drive.status() == (DriveStatus{true, true, false, Density::Double}));
}

static void test_read_success_and_failure_update_status(void)
{
    FakeByteSource source = sd_source();
    VirtualDrive drive = make_d1();
    mount_ok(drive, open_ok(source));
    std::array<uint8_t, 128> buffer{};

    TEST_ASSERT_TRUE(drive.readSector(sector(721), Span<uint8_t>{buffer}).error() == ErrorCode::SectorOutOfRange);
    TEST_ASSERT_TRUE(drive.status().lastOperationFailed());

    Result<uint16_t> ok = drive.readSector(sector(1), Span<uint8_t>{buffer});
    TEST_ASSERT_EQUAL_UINT16(128, ok.value());
    TEST_ASSERT_FALSE(drive.status().lastOperationFailed());
}

static void test_mount_clears_previous_failure(void)
{
    FakeByteSource source = sd_source();
    VirtualDrive drive = make_d1();
    std::array<uint8_t, 128> buffer{};
    (void)drive.readSector(sector(1), Span<uint8_t>{buffer});
    TEST_ASSERT_TRUE(drive.status().lastOperationFailed());
    mount_ok(drive, open_ok(source));
    TEST_ASSERT_FALSE(drive.status().lastOperationFailed());
}

static void test_mount_rejects_moved_from_image(void)
{
    FakeByteSource source = sd_source();
    VirtualDrive drive = make_d1();
    DiskImage image = open_ok(source);
    DiskImage keeper{std::move(image)};
    Result<ImageByteSource*> result = drive.mount(std::move(image));  // NOLINT(bugprone-use-after-move)
    TEST_ASSERT_TRUE(result.error() == ErrorCode::InvalidDiskImage);
    TEST_ASSERT_FALSE(drive.hasDisk());
}

// --- Ownership rules (H1-8) ------------------------------------------------------

static void test_mounting_over_a_disk_returns_the_detached_previous_source(void)
{
    FakeByteSource first = sd_source();
    FakeByteSource second = dd_source();
    VirtualDrive drive = make_d1();
    mount_ok(drive, open_ok(first));

    ImageByteSource* displaced = mount_ok(drive, open_ok(second));
    TEST_ASSERT_EQUAL_PTR(&first, displaced);
    TEST_ASSERT_FALSE(first.isAttached());  // safe for the repository to close
    TEST_ASSERT_TRUE(second.isAttached());
    TEST_ASSERT_TRUE(drive.status().density() == Density::Double);
}

static void test_mounted_source_cannot_be_opened_again(void)
{
    // Problem 1: the same instance cannot be mounted twice, in this or any drive.
    FakeByteSource source = sd_source();
    VirtualDrive drive = make_d1();
    mount_ok(drive, open_ok(source));
    TEST_ASSERT_TRUE(DiskImage::open(source).error() == ErrorCode::SourceInUse);
}

static void test_source_stays_attached_while_mounted(void)
{
    // Problem 2: a mounted source reports itself in use, so the repository must
    // not close it (ImageRepository contract, H2).
    FakeByteSource source = sd_source();
    VirtualDrive drive = make_d1();
    mount_ok(drive, open_ok(source));
    TEST_ASSERT_TRUE(source.isAttached());
}

static void test_unmount_returns_detached_source_once(void)
{
    FakeByteSource source = sd_source();
    VirtualDrive drive = make_d1();
    mount_ok(drive, open_ok(source));

    TEST_ASSERT_EQUAL_PTR(&source, drive.unmount());
    TEST_ASSERT_FALSE(source.isAttached());
    TEST_ASSERT_FALSE(drive.hasDisk());
    TEST_ASSERT_NULL(drive.unmount());  // second call: nothing to close
}

static void test_unmounted_source_is_never_read_again(void)
{
    // Problem 4: after unmount the drive holds no reference to the source.
    FakeByteSource source = sd_source();
    VirtualDrive drive = make_d1();
    mount_ok(drive, open_ok(source));
    (void)drive.unmount();
    const unsigned reads_before = source.reads();

    std::array<uint8_t, 128> buffer{};
    TEST_ASSERT_TRUE(drive.readSector(sector(1), Span<uint8_t>{buffer}).error() == ErrorCode::NoDiskMounted);
    TEST_ASSERT_EQUAL_UINT(reads_before, source.reads());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_new_drive_is_empty);
    RUN_TEST(test_read_on_empty_drive_fails_and_is_reported);
    RUN_TEST(test_mount_reports_disk_and_density);
    RUN_TEST(test_read_success_and_failure_update_status);
    RUN_TEST(test_mount_clears_previous_failure);
    RUN_TEST(test_mount_rejects_moved_from_image);
    RUN_TEST(test_mounting_over_a_disk_returns_the_detached_previous_source);
    RUN_TEST(test_mounted_source_cannot_be_opened_again);
    RUN_TEST(test_source_stays_attached_while_mounted);
    RUN_TEST(test_unmount_returns_detached_source_once);
    RUN_TEST(test_unmounted_source_is_never_read_again);
    return UNITY_END();
}
