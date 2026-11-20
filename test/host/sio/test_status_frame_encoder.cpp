// StatusFrameEncoder: the 4-byte STATUS response (architecture §4.2).
// Tag of each expectation: [DOC] documented, [DIS] design decision,
// [HIP] provisional, pending physical validation (H6/H7).

#include <array>
#include <cstdint>

#include "rb/domain/disk_geometry.hpp"
#include "rb/domain/drive_status.hpp"
#include "rb/sio/status_frame_encoder.hpp"
#include "unity.h"

using rb::domain::Density;
using rb::domain::DiskGeometry;
using rb::domain::DriveStatus;
using rb::domain::SectorSize;
using rb::sio::StatusFrameEncoder;

namespace {

using Frame = std::array<uint8_t, 4>;

DiskGeometry geometry(uint32_t sectors, SectorSize size)
{
    return DiskGeometry::create(sectors, size).value();
}

Frame encode_mounted(uint32_t sectors, SectorSize size, bool last_failed = false, bool rejected = false)
{
    const DiskGeometry g = geometry(sectors, size);
    return StatusFrameEncoder::encode(DriveStatus{true, true, last_failed, g.density()}, &g, rejected);
}

Frame encode_empty(bool last_failed = false, bool rejected = false)
{
    return StatusFrameEncoder::encode(DriveStatus{false, true, last_failed, Density::Unknown}, nullptr, rejected);
}

void assert_frame(const Frame& expected, const Frame& actual)
{
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.data(), actual.data(), expected.size());
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_empty_drive(void)
{
    // byte 1 $7F: "not ready" [DIS, 1050-style]; byte 2 $E0 [DOC 810]; byte 3 $00 [DOC]
    assert_frame(Frame{0x00, 0x7F, 0xE0, 0x00}, encode_empty());
}

static void test_single_density(void)
{
    // byte 1 $FF: no FDC error [DOC 810]; no density bits
    assert_frame(Frame{0x00, 0xFF, 0xE0, 0x00}, encode_mounted(720, SectorSize::Bytes128));
}

static void test_enhanced_density_sets_d7(void)
{
    assert_frame(Frame{0x80, 0xFF, 0xE0, 0x00}, encode_mounted(1040, SectorSize::Bytes128));  // [DOC ALT]
}

static void test_double_density_sets_d5(void)
{
    assert_frame(Frame{0x20, 0xFF, 0xE0, 0x00}, encode_mounted(720, SectorSize::Bytes256));  // [DOC ALT]
}

static void test_d5_follows_sector_size_not_sector_count(void)
{
    // D5 means "256-byte sectors": also for non-standard double-density images.
    assert_frame(Frame{0x20, 0xFF, 0xE0, 0x00}, encode_mounted(1440, SectorSize::Bytes256));
    assert_frame(Frame{0x00, 0xFF, 0xE0, 0x00}, encode_mounted(100, SectorSize::Bytes128));
}

static void test_previous_failure_reports_record_not_found(void)
{
    // byte 1 $EF: record not found [DIS on a DOC 810 value]
    assert_frame(Frame{0x00, 0xEF, 0xE0, 0x00}, encode_mounted(720, SectorSize::Bytes128, true));
}

static void test_no_disk_takes_precedence_over_previous_failure(void)
{
    assert_frame(Frame{0x00, 0x7F, 0xE0, 0x00}, encode_empty(true));
}

static void test_previous_nak_sets_d0(void)
{
    // D0 after a NAK to this drive [HIP]
    assert_frame(Frame{0x01, 0xFF, 0xE0, 0x00}, encode_mounted(720, SectorSize::Bytes128, false, true));
    assert_frame(Frame{0x01, 0x7F, 0xE0, 0x00}, encode_empty(false, true));
}

static void test_d3_is_not_a_permanent_write_protect_flag(void)
{
    // D3 = "failed due to write protected disk" [DOC ALT]. v1 never attempts a
    // write (write commands are NAKed), so D3 stays 0 even though the domain
    // reports the drive as write-protected. D4 (motor) stays 0 [DIS].
    const Frame frame = encode_mounted(720, SectorSize::Bytes256, true, true);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[0] & 0x08);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[0] & 0x10);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[0] & 0x46);  // D1, D2, D6 never set in v1
}

static void test_protection_is_reported_in_byte_1_only_with_writes(void)
{
    const DiskGeometry g = geometry(720, SectorSize::Bytes128);
    const DriveStatus locked{true, true, false, g.density()};
    const DriveStatus open{true, false, false, g.density()};
    assert_frame(Frame{0x00, 0xFF, 0xE0, 0x00}, StatusFrameEncoder::encode(locked, &g, false, 0, false));
    assert_frame(Frame{0x00, 0xBF, 0xE0, 0x00}, StatusFrameEncoder::encode(locked, &g, false, 0, true));
    assert_frame(Frame{0x00, 0xFF, 0xE0, 0x00}, StatusFrameEncoder::encode(open, &g, false, 0, true));
    // An empty drive stays "not ready".
    const DriveStatus empty{false, true, false, Density::Unknown};
    assert_frame(Frame{0x00, 0x7F, 0xE0, 0x00}, StatusFrameEncoder::encode(empty, nullptr, false, 0, true));
}

static void test_write_flags_set_d1_d2_d3(void)
{
    const DiskGeometry g = geometry(720, SectorSize::Bytes128);
    const DriveStatus locked_failed{true, true, true, g.density()};
    const DriveStatus open_failed{true, false, true, g.density()};
    // Refused by protection: D3, and byte 1 stays $BF (not "record not found").
    assert_frame(Frame{0x08, 0xBF, 0xE0, 0x00},
                 StatusFrameEncoder::encode(locked_failed, &g, false, StatusFrameEncoder::kFlagProtected, true));
    // Failed write on a writable disk: D2 and $EF.
    assert_frame(Frame{0x04, 0xEF, 0xE0, 0x00},
                 StatusFrameEncoder::encode(open_failed, &g, false, StatusFrameEncoder::kFlagPut, true));
    // Bad data frame: D1. Bits outside D1-D3 are never taken from the flags.
    assert_frame(Frame{0x02, 0xFF, 0xE0, 0x00},
                 StatusFrameEncoder::encode(DriveStatus{true, false, false, g.density()}, &g, false, 0xF3, true));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_drive);
    RUN_TEST(test_single_density);
    RUN_TEST(test_enhanced_density_sets_d7);
    RUN_TEST(test_double_density_sets_d5);
    RUN_TEST(test_d5_follows_sector_size_not_sector_count);
    RUN_TEST(test_previous_failure_reports_record_not_found);
    RUN_TEST(test_no_disk_takes_precedence_over_previous_failure);
    RUN_TEST(test_previous_nak_sets_d0);
    RUN_TEST(test_d3_is_not_a_permanent_write_protect_flag);
    RUN_TEST(test_protection_is_reported_in_byte_1_only_with_writes);
    RUN_TEST(test_write_flags_set_d1_d2_d3);
    return UNITY_END();
}
