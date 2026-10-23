// Use cases across the two contexts of ADR-014, simulated in one thread:
//   storage context: MountDiskImage, UnmountDiskImage, CollectDriveReply
//   drive context:   ApplyDriveRequest, GetDriveStatus, ReadSector
// Every scenario ends by checking that no source leaked, none was closed twice
// and no attached slot was reused.

#include <array>
#include <cstdint>
#include <optional>

#include "fakes/fake_drive_mailbox.hpp"
#include "fakes/memory_image_repository.hpp"
#include "rb/app/drive_bay.hpp"
#include "rb/app/usecases/apply_drive_request.hpp"
#include "rb/app/usecases/collect_drive_reply.hpp"
#include "rb/app/usecases/get_drive_status.hpp"
#include "rb/app/usecases/mount_disk_image.hpp"
#include "rb/app/usecases/read_sector.hpp"
#include "rb/app/usecases/unmount_disk_image.hpp"
#include "support/atr_header.hpp"
#include "unity.h"

using rb::app::ApplyDriveRequest;
using rb::app::CollectDriveReply;
using rb::app::DriveBay;
using rb::app::DriveOutcome;
using rb::app::DriveRequestKind;
using rb::app::GetDriveStatus;
using rb::app::ImageLocation;
using rb::app::MountDiskImage;
using rb::app::MountReport;
using rb::app::ReadSector;
using rb::app::UnmountDiskImage;
using rb::core::ErrorCode;
using rb::core::Result;
using rb::core::Span;
using rb::domain::Density;
using rb::domain::DriveNumber;
using rb::domain::DriveStatus;
using rb::domain::SectorNumber;
using rb::test::dd_data_size;
using rb::test::FakeDriveMailbox;
using rb::test::kAtrHeaderSize;
using rb::test::make_atr_header;
using rb::test::MemoryImage;
using rb::test::MemoryImageRepository;
using rb::test::sd_data_size;
using rb::test::SlotByteSource;

namespace {

std::array<uint8_t, 16> bad_header()
{
    auto header = make_atr_header(sd_data_size(720), 128);
    header[0] = 0x00;
    return header;
}

const MemoryImage kSd{"sd.atr", make_atr_header(sd_data_size(720), 128), kAtrHeaderSize + sd_data_size(720)};
const MemoryImage kDd{"dd.atr", make_atr_header(dd_data_size(720), 256), kAtrHeaderSize + dd_data_size(720)};
const MemoryImage kBad{"bad.atr", bad_header(), kAtrHeaderSize + sd_data_size(720)};

DriveNumber drive(uint8_t n)
{
    return DriveNumber::from(n).value();
}

ImageLocation location(const char* text)
{
    return ImageLocation::from(text).value();
}

SectorNumber sector(uint16_t n)
{
    return SectorNumber::from(n).value();
}

// Wiring of one drive bay, a repository with `Slots` slots and a mailbox.
template <std::size_t Slots>
struct Harness {
    MemoryImageRepository<Slots> repository;
    FakeDriveMailbox mailbox;
    DriveBay bay{1};

    // storage context
    MountDiskImage mount{repository, mailbox};
    UnmountDiskImage unmount{mailbox};
    CollectDriveReply collect{repository, mailbox};
    // drive context
    ApplyDriveRequest apply{bay, mailbox};
    GetDriveStatus status{bay};
    ReadSector read{bay};

    Harness()
    {
        repository.add(kSd);
        repository.add(kDd);
        repository.add(kBad);
    }

    // Runs the drive context and then the storage context once.
    DriveOutcome round_trip()
    {
        TEST_ASSERT_TRUE_MESSAGE(apply.execute(), "expected a pending drive request");
        std::optional<DriveOutcome> outcome = collect.execute();
        TEST_ASSERT_TRUE_MESSAGE(outcome.has_value(), "expected a drive reply");
        return *outcome;
    }

    void mount_ok(uint8_t n, const char* name)
    {
        TEST_ASSERT_TRUE(mount.execute(drive(n), location(name)).ok());
        DriveOutcome outcome = round_trip();
        TEST_ASSERT_FALSE(outcome.error.has_value());
    }

    // No leak, no double close: every open source is either mounted or in flight.
    void check_sources(unsigned expected_open)
    {
        TEST_ASSERT_EQUAL_UINT_MESSAGE(expected_open, repository.open_count(), "unexpected number of open sources");
        TEST_ASSERT_EQUAL_UINT_MESSAGE(repository.opens() - repository.closes(), repository.open_count(),
                                       "a source was closed twice or never closed");
    }
};

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- Mount / unmount -------------------------------------------------------------

static void test_mount_is_applied_only_in_the_drive_context(void)
{
    Harness<2> h;
    Result<MountReport> report = h.mount.execute(drive(1), location("sd.atr"));
    TEST_ASSERT_TRUE(report.ok());
    TEST_ASSERT_EQUAL_UINT32(720, report.value().geometry.sectorCount());
    TEST_ASSERT_FALSE(report.value().headerSizeMismatch);
    TEST_ASSERT_FALSE(h.bay.find(drive(1))->hasDisk());  // prepared, not yet applied

    DriveOutcome outcome = h.round_trip();
    TEST_ASSERT_TRUE(outcome.kind == DriveRequestKind::Mount);
    TEST_ASSERT_FALSE(outcome.error.has_value());
    TEST_ASSERT_TRUE(h.bay.find(drive(1))->hasDisk());
    TEST_ASSERT_EQUAL_STRING("sd.atr", h.bay.mountedLocation(drive(1))->c_str());
    h.check_sources(1);
}

static void test_remount_closes_the_displaced_source_once(void)
{
    Harness<2> h;
    h.mount_ok(1, "sd.atr");
    h.mount_ok(1, "dd.atr");
    TEST_ASSERT_EQUAL_UINT(1, h.repository.closes());
    TEST_ASSERT_TRUE(h.status.execute(drive(1)).value().density() == Density::Double);
    h.check_sources(1);
}

static void test_unmount_closes_the_source_and_frees_the_slot(void)
{
    Harness<1> h;
    h.mount_ok(1, "sd.atr");
    TEST_ASSERT_TRUE(h.unmount.execute(drive(1)).ok());
    DriveOutcome outcome = h.round_trip();
    TEST_ASSERT_TRUE(outcome.kind == DriveRequestKind::Unmount);
    TEST_ASSERT_FALSE(outcome.error.has_value());
    TEST_ASSERT_NULL(h.bay.mountedLocation(drive(1)));
    h.check_sources(0);

    h.mount_ok(1, "dd.atr");  // the single slot is reusable
    h.check_sources(1);
}

static void test_unmount_of_an_empty_drive_closes_nothing(void)
{
    Harness<1> h;
    TEST_ASSERT_TRUE(h.unmount.execute(drive(1)).ok());
    DriveOutcome outcome = h.round_trip();
    TEST_ASSERT_FALSE(outcome.error.has_value());
    TEST_ASSERT_EQUAL_UINT(0, h.repository.closes());
    h.check_sources(0);
}

// --- Cancelled or failed mounts never lose a source --------------------------------

static void test_invalid_image_is_closed_before_reaching_the_drive(void)
{
    Harness<1> h;
    TEST_ASSERT_TRUE(h.mount.execute(drive(1), location("bad.atr")).error() == ErrorCode::InvalidAtrHeader);
    TEST_ASSERT_TRUE(h.mailbox.idle());
    TEST_ASSERT_EQUAL_UINT(1, h.repository.opens());
    h.check_sources(0);
}

static void test_missing_image_opens_nothing(void)
{
    Harness<1> h;
    TEST_ASSERT_TRUE(h.mount.execute(drive(1), location("none.atr")).error() == ErrorCode::NotFound);
    TEST_ASSERT_EQUAL_UINT(0, h.repository.opens());
    h.check_sources(0);
}

static void test_remount_needs_a_free_slot(void)
{
    // With one slot, the new image cannot be opened while the old one is mounted:
    // the repository needs (drives + 1) slots for remounting without unmounting.
    Harness<1> h;
    h.mount_ok(1, "sd.atr");
    TEST_ASSERT_TRUE(h.mount.execute(drive(1), location("dd.atr")).error() == ErrorCode::NoFreeSlot);
    TEST_ASSERT_TRUE(h.status.execute(drive(1)).value().density() == Density::Single);
    h.check_sources(1);
}

static void test_busy_mailbox_cancels_the_second_mount_cleanly(void)
{
    Harness<2> h;
    TEST_ASSERT_TRUE(h.mount.execute(drive(1), location("sd.atr")).ok());   // in flight
    TEST_ASSERT_TRUE(h.mount.execute(drive(1), location("dd.atr")).error() == ErrorCode::MailboxBusy);
    TEST_ASSERT_EQUAL_UINT(1, h.repository.closes());  // the cancelled source was closed
    h.check_sources(1);

    DriveOutcome outcome = h.round_trip();  // the first mount is unaffected
    TEST_ASSERT_FALSE(outcome.error.has_value());
    TEST_ASSERT_TRUE(h.status.execute(drive(1)).value().density() == Density::Single);
    h.check_sources(1);
}

static void test_rejected_mount_returns_its_source_for_closing(void)
{
    Harness<1> h;  // D2 is not configured: the storage context cannot know that
    TEST_ASSERT_TRUE(h.mount.execute(drive(2), location("sd.atr")).ok());
    DriveOutcome outcome = h.round_trip();
    TEST_ASSERT_TRUE(outcome.error.has_value());
    TEST_ASSERT_TRUE(*outcome.error == ErrorCode::DriveNotConfigured);
    TEST_ASSERT_EQUAL_UINT(1, h.repository.closes());
    h.check_sources(0);

    h.mount_ok(1, "sd.atr");  // the slot was not lost
    h.check_sources(1);
}

static void test_rejected_unmount_closes_nothing(void)
{
    Harness<1> h;
    h.mount_ok(1, "sd.atr");
    TEST_ASSERT_TRUE(h.unmount.execute(drive(2)).ok());
    DriveOutcome outcome = h.round_trip();
    TEST_ASSERT_TRUE(*outcome.error == ErrorCode::DriveNotConfigured);
    TEST_ASSERT_EQUAL_UINT(0, h.repository.closes());
    h.check_sources(1);
}

// --- Context separation ---------------------------------------------------------------

static void test_drive_context_never_uses_the_repository(void)
{
    Harness<2> h;
    TEST_ASSERT_TRUE(h.mount.execute(drive(1), location("sd.atr")).ok());
    unsigned before = h.repository.operations();
    TEST_ASSERT_TRUE(h.apply.execute());
    TEST_ASSERT_EQUAL_UINT(before, h.repository.operations());

    (void)h.collect.execute();
    TEST_ASSERT_TRUE(h.mount.execute(drive(1), location("dd.atr")).ok());
    before = h.repository.operations();
    TEST_ASSERT_TRUE(h.apply.execute());  // displaces sd.atr without closing it
    TEST_ASSERT_EQUAL_UINT(before, h.repository.operations());
    TEST_ASSERT_EQUAL_UINT(2, h.repository.open_count());  // closed later, by the storage context

    (void)h.collect.execute();
    h.check_sources(1);
}

static void test_nothing_pending(void)
{
    Harness<1> h;
    TEST_ASSERT_FALSE(h.apply.execute());
    TEST_ASSERT_FALSE(h.collect.execute().has_value());
}

// --- Drive context queries ------------------------------------------------------------

static void test_drive_status(void)
{
    Harness<1> h;
    TEST_ASSERT_TRUE(h.status.execute(drive(1)).value() == (DriveStatus{false, true, false, Density::Unknown}));
    TEST_ASSERT_TRUE(h.status.execute(drive(2)).error() == ErrorCode::DriveNotConfigured);
    h.mount_ok(1, "sd.atr");
    TEST_ASSERT_TRUE(h.status.execute(drive(1)).value() == (DriveStatus{true, true, false, Density::Single}));
}

static void check_sector(const std::array<uint8_t, 256>& buffer, uint32_t offset, uint16_t length)
{
    for (uint16_t i = 0; i < length; ++i) {
        TEST_ASSERT_EQUAL_UINT8(SlotByteSource::data_byte(offset + i), buffer[i]);
    }
}

static void test_read_sectors_of_128_and_256_bytes(void)
{
    Harness<2> h;
    std::array<uint8_t, 256> buffer{};
    TEST_ASSERT_TRUE(h.read.execute(drive(1), sector(1), Span<uint8_t>{buffer}).error() == ErrorCode::NoDiskMounted);

    h.mount_ok(1, "sd.atr");
    TEST_ASSERT_EQUAL_UINT16(128, h.read.execute(drive(1), sector(720), Span<uint8_t>{buffer}).value());
    check_sector(buffer, kAtrHeaderSize + 719 * 128, 128);

    h.mount_ok(1, "dd.atr");
    TEST_ASSERT_EQUAL_UINT16(128, h.read.execute(drive(1), sector(3), Span<uint8_t>{buffer}).value());
    TEST_ASSERT_EQUAL_UINT16(256, h.read.execute(drive(1), sector(4), Span<uint8_t>{buffer}).value());
    check_sector(buffer, kAtrHeaderSize + 3 * 128, 256);
}

static void test_read_errors(void)
{
    Harness<1> h;
    h.mount_ok(1, "sd.atr");
    std::array<uint8_t, 128> buffer{};
    TEST_ASSERT_TRUE(h.read.execute(drive(1), sector(721), Span<uint8_t>{buffer}).error() == ErrorCode::SectorOutOfRange);
    TEST_ASSERT_TRUE(h.read.execute(drive(2), sector(1), Span<uint8_t>{buffer}).error() == ErrorCode::DriveNotConfigured);

    h.repository.slot_source(0).fail_reads(true);
    TEST_ASSERT_TRUE(h.read.execute(drive(1), sector(1), Span<uint8_t>{buffer}).error() == ErrorCode::IoError);
    TEST_ASSERT_TRUE(h.status.execute(drive(1)).value().lastOperationFailed());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mount_is_applied_only_in_the_drive_context);
    RUN_TEST(test_remount_closes_the_displaced_source_once);
    RUN_TEST(test_unmount_closes_the_source_and_frees_the_slot);
    RUN_TEST(test_unmount_of_an_empty_drive_closes_nothing);
    RUN_TEST(test_invalid_image_is_closed_before_reaching_the_drive);
    RUN_TEST(test_missing_image_opens_nothing);
    RUN_TEST(test_remount_needs_a_free_slot);
    RUN_TEST(test_busy_mailbox_cancels_the_second_mount_cleanly);
    RUN_TEST(test_rejected_mount_returns_its_source_for_closing);
    RUN_TEST(test_rejected_unmount_closes_nothing);
    RUN_TEST(test_drive_context_never_uses_the_repository);
    RUN_TEST(test_nothing_pending);
    RUN_TEST(test_drive_status);
    RUN_TEST(test_read_sectors_of_128_and_256_bytes);
    RUN_TEST(test_read_errors);
    return UNITY_END();
}
