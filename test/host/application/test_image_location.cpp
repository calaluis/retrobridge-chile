// ImageLocation (H2-9): bounded, allocation-free location of a disk image.

#include <array>
#include <cstring>

#include "rb/app/image_location.hpp"
#include "unity.h"

using rb::app::ImageLocation;
using rb::core::ErrorCode;

void setUp(void) {}
void tearDown(void) {}

static void test_accepts_printable_ascii(void)
{
    const auto result = ImageLocation::from("/sdcard/atr/dos25.atr");
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_STRING("/sdcard/atr/dos25.atr", result.value().c_str());
    TEST_ASSERT_EQUAL_size_t(21, result.value().length());
}

static void test_rejects_null_and_empty(void)
{
    TEST_ASSERT_TRUE(ImageLocation::from(nullptr).error() == ErrorCode::InvalidLocation);
    TEST_ASSERT_TRUE(ImageLocation::from("").error() == ErrorCode::InvalidLocation);
}

static void test_maximum_length(void)
{
    std::array<char, ImageLocation::kMaxLength + 2> text{};
    std::memset(text.data(), 'a', ImageLocation::kMaxLength);
    const auto longest = ImageLocation::from(text.data());
    TEST_ASSERT_TRUE(longest.ok());
    TEST_ASSERT_EQUAL_size_t(ImageLocation::kMaxLength, longest.value().length());

    text[ImageLocation::kMaxLength] = 'a';  // one character too many
    TEST_ASSERT_TRUE(ImageLocation::from(text.data()).error() == ErrorCode::InvalidLocation);
}

static void test_rejects_non_printable_characters(void)
{
    TEST_ASSERT_TRUE(ImageLocation::from("a\tb").error() == ErrorCode::InvalidLocation);
    TEST_ASSERT_TRUE(ImageLocation::from("a\x7F").error() == ErrorCode::InvalidLocation);
    TEST_ASSERT_TRUE(ImageLocation::from("caf\xC3\xA9").error() == ErrorCode::InvalidLocation);
}

static void test_equality(void)
{
    const ImageLocation a = ImageLocation::from("disk.atr").value();
    const ImageLocation b = ImageLocation::from("disk.atr").value();
    const ImageLocation c = ImageLocation::from("disk2.atr").value();
    TEST_ASSERT_TRUE(a == b);
    TEST_ASSERT_TRUE(a != c);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_accepts_printable_ascii);
    RUN_TEST(test_rejects_null_and_empty);
    RUN_TEST(test_maximum_length);
    RUN_TEST(test_rejects_non_printable_characters);
    RUN_TEST(test_equality);
    return UNITY_END();
}
