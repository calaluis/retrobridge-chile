// SIO checksum: arithmetic sum with the carry added back after every addition
// (Atari C016555 §9). Vectors computed by hand.

#include <array>
#include <cstdint>

#include "rb/sio/sio_checksum.hpp"
#include "unity.h"

using rb::core::Span;
using rb::sio::sio_checksum;

namespace {

template <std::size_t N>
uint8_t checksum(const std::array<uint8_t, N>& bytes)
{
    return sio_checksum(Span<const uint8_t>{bytes});
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_empty_sequence_is_zero(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00, sio_checksum(Span<const uint8_t>{}));
}

static void test_sum_without_carry(void)
{
    // D1 STATUS: $31 + $53 = $84
    TEST_ASSERT_EQUAL_HEX8(0x84, checksum(std::array<uint8_t, 4>{0x31, 0x53, 0x00, 0x00}));
    // D1 READ sector 1: $31 + $52 + $01 = $84
    TEST_ASSERT_EQUAL_HEX8(0x84, checksum(std::array<uint8_t, 4>{0x31, 0x52, 0x01, 0x00}));
}

static void test_carry_is_added_back(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x01, checksum(std::array<uint8_t, 2>{0xFF, 0x01}));  // $100 -> $00 + 1
    TEST_ASSERT_EQUAL_HEX8(0x01, checksum(std::array<uint8_t, 2>{0x80, 0x80}));  // $100 -> $00 + 1
    TEST_ASSERT_EQUAL_HEX8(0xFF, checksum(std::array<uint8_t, 2>{0xFF, 0xFF}));  // $1FE -> $FE + 1
}

static void test_carry_in_a_command_frame(void)
{
    // D1 READ sector 720 ($02D0): $31+$52 = $83; +$D0 = $153 -> $54; +$02 = $56
    TEST_ASSERT_EQUAL_HEX8(0x56, checksum(std::array<uint8_t, 4>{0x31, 0x52, 0xD0, 0x02}));
}

static void test_long_frame_of_ff(void)
{
    std::array<uint8_t, 128> all_ff{};
    all_ff.fill(0xFF);
    TEST_ASSERT_EQUAL_HEX8(0xFF, checksum(all_ff));

    std::array<uint8_t, 256> zeros{};
    TEST_ASSERT_EQUAL_HEX8(0x00, checksum(zeros));
}

static void test_status_frames(void)
{
    // $7F + $E0 = $15F -> $5F + 1 = $60
    TEST_ASSERT_EQUAL_HEX8(0x60, checksum(std::array<uint8_t, 4>{0x00, 0x7F, 0xE0, 0x00}));
    // $20 + $FF = $11F -> $20; + $E0 = $100 -> $01
    TEST_ASSERT_EQUAL_HEX8(0x01, checksum(std::array<uint8_t, 4>{0x20, 0xFF, 0xE0, 0x00}));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_sequence_is_zero);
    RUN_TEST(test_sum_without_carry);
    RUN_TEST(test_carry_is_added_back);
    RUN_TEST(test_carry_in_a_command_frame);
    RUN_TEST(test_long_frame_of_ff);
    RUN_TEST(test_status_frames);
    return UNITY_END();
}
