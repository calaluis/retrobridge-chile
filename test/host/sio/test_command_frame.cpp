// CommandFrame: the five bytes sent while COMMAND is low (device ID, command,
// AUX1, AUX2, checksum).

#include <array>
#include <cstdint>

#include "rb/sio/command_frame.hpp"
#include "unity.h"

using rb::core::ErrorCode;
using rb::core::Span;
using rb::sio::CommandFrame;

namespace {

template <std::size_t N>
rb::core::Result<CommandFrame> parse(const std::array<uint8_t, N>& bytes)
{
    return CommandFrame::parse(Span<const uint8_t>{bytes});
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_valid_read_frame(void)
{
    const auto frame = parse(std::array<uint8_t, 5>{0x31, 0x52, 0xD0, 0x02, 0x56});
    TEST_ASSERT_TRUE(frame.ok());
    TEST_ASSERT_EQUAL_HEX8(0x31, frame.value().deviceId());
    TEST_ASSERT_EQUAL_HEX8(0x52, frame.value().command());
    TEST_ASSERT_EQUAL_HEX8(0xD0, frame.value().aux1());
    TEST_ASSERT_EQUAL_HEX8(0x02, frame.value().aux2());
    TEST_ASSERT_EQUAL_UINT16(720, frame.value().aux());  // AUX1 = LSB, AUX2 = MSB
}

static void test_bad_checksum_is_rejected(void)
{
    TEST_ASSERT_TRUE(parse(std::array<uint8_t, 5>{0x31, 0x52, 0xD0, 0x02, 0x55}).error() == ErrorCode::ChecksumMismatch);
    TEST_ASSERT_TRUE(parse(std::array<uint8_t, 5>{0x31, 0x53, 0x00, 0x00, 0x00}).error() == ErrorCode::ChecksumMismatch);
}

static void test_wrong_length_is_rejected(void)
{
    TEST_ASSERT_TRUE(parse(std::array<uint8_t, 4>{0x31, 0x53, 0x00, 0x00}).error() == ErrorCode::InvalidCommandFrame);
    TEST_ASSERT_TRUE(parse(std::array<uint8_t, 6>{0x31, 0x53, 0x00, 0x00, 0x84, 0x00}).error() ==
                     ErrorCode::InvalidCommandFrame);
    TEST_ASSERT_TRUE(CommandFrame::parse(Span<const uint8_t>{}).error() == ErrorCode::InvalidCommandFrame);
}

static void test_frame_for_another_device_still_parses(void)
{
    // Addressing is the dispatcher's decision, not the frame's: $40 is the printer.
    const auto frame = parse(std::array<uint8_t, 5>{0x40, 0x53, 0x00, 0x00, 0x93});
    TEST_ASSERT_TRUE(frame.ok());
    TEST_ASSERT_EQUAL_HEX8(0x40, frame.value().deviceId());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_read_frame);
    RUN_TEST(test_bad_checksum_is_rejected);
    RUN_TEST(test_wrong_length_is_rejected);
    RUN_TEST(test_frame_for_another_device_still_parses);
    return UNITY_END();
}
