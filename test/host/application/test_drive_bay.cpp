// DriveBay: fixed set of configured drives D1..Dn plus the location mounted in each.

#include <cstring>
#include <type_traits>
#include <utility>

#include "fakes/fake_byte_source.hpp"
#include "rb/app/drive_bay.hpp"
#include "support/atr_header.hpp"
#include "unity.h"

using rb::app::DriveBay;
using rb::app::ImageLocation;
using rb::core::ErrorCode;
using rb::domain::DiskImage;
using rb::domain::DriveNumber;
using rb::domain::ImageByteSource;
using rb::test::FakeByteSource;
using rb::test::kAtrHeaderSize;
using rb::test::make_atr_header;
using rb::test::sd_data_size;

static_assert(!std::is_copy_constructible<DriveBay>::value, "DriveBay must not be copyable");
static_assert(!std::is_move_constructible<DriveBay>::value, "DriveBay must not be movable");

namespace {

FakeByteSource sd_source()
{
    return FakeByteSource{make_atr_header(sd_data_size(720), 128), kAtrHeaderSize + sd_data_size(720)};
}

DriveNumber drive(uint8_t n)
{
    return DriveNumber::from(n).value();
}

ImageLocation location(const char* text)
{
    return ImageLocation::from(text).value();
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_only_configured_drives_exist(void)
{
    DriveBay bay{1};
    TEST_ASSERT_EQUAL_UINT8(1, bay.driveCount());
    TEST_ASSERT_NOT_NULL(bay.find(drive(1)));
    TEST_ASSERT_EQUAL_UINT8(1, bay.find(drive(1))->number().value());
    TEST_ASSERT_NULL(bay.find(drive(2)));

    DriveBay full{8};
    TEST_ASSERT_NOT_NULL(full.find(drive(8)));
}

static void test_mount_records_location_and_unmount_clears_it(void)
{
    FakeByteSource source = sd_source();
    DriveBay bay{1};
    TEST_ASSERT_NULL(bay.mountedLocation(drive(1)));

    auto mounted = bay.mount(drive(1), DiskImage::open(source).value(), location("sd.atr"));
    TEST_ASSERT_TRUE(mounted.ok());
    TEST_ASSERT_NULL(mounted.value());
    TEST_ASSERT_EQUAL_STRING("sd.atr", bay.mountedLocation(drive(1))->c_str());
    TEST_ASSERT_TRUE(bay.find(drive(1))->hasDisk());

    auto unmounted = bay.unmount(drive(1));
    TEST_ASSERT_EQUAL_PTR(&source, unmounted.value());
    TEST_ASSERT_FALSE(source.isAttached());
    TEST_ASSERT_NULL(bay.mountedLocation(drive(1)));
}

static void test_remount_returns_displaced_source_and_updates_location(void)
{
    FakeByteSource first = sd_source();
    FakeByteSource second = sd_source();
    DriveBay bay{1};
    (void)bay.mount(drive(1), DiskImage::open(first).value(), location("a.atr"));
    auto result = bay.mount(drive(1), DiskImage::open(second).value(), location("b.atr"));
    TEST_ASSERT_EQUAL_PTR(&first, result.value());
    TEST_ASSERT_EQUAL_STRING("b.atr", bay.mountedLocation(drive(1))->c_str());
}

static void test_unconfigured_drive_does_not_consume_the_image(void)
{
    FakeByteSource source = sd_source();
    DriveBay bay{1};
    DiskImage image = DiskImage::open(source).value();
    auto result = bay.mount(drive(2), std::move(image), location("sd.atr"));
    TEST_ASSERT_TRUE(result.error() == ErrorCode::DriveNotConfigured);
    TEST_ASSERT_TRUE(image.isValid());  // NOLINT(bugprone-use-after-move): not consumed on failure
    TEST_ASSERT_TRUE(source.isAttached());
    TEST_ASSERT_TRUE(bay.unmount(drive(2)).error() == ErrorCode::DriveNotConfigured);
}

// Death test: the drive count is configuration, so an invalid value aborts.
static int construct_with_invalid_count(void)
{
    DriveBay bay{9};
    return bay.driveCount() == 9 ? 0 : 1;
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--invalid-drive-count") == 0) {
        return construct_with_invalid_count();
    }

    UNITY_BEGIN();
    RUN_TEST(test_only_configured_drives_exist);
    RUN_TEST(test_mount_records_location_and_unmount_clears_it);
    RUN_TEST(test_remount_returns_displaced_source_and_updates_location);
    RUN_TEST(test_unconfigured_drive_does_not_consume_the_image);
    return UNITY_END();
}
