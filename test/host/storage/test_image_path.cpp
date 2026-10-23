// Path policy of the file adapter (H3-2, ADR-015): an ImageLocation is relative
// to the storage base; anything that could escape it or is ambiguous is rejected.

#include <array>
#include <cstring>

#include "rb/app/image_location.hpp"
#include "rb/storage/image_path.hpp"
#include "unity.h"

using rb::app::ImageLocation;
using rb::core::ErrorCode;
using rb::core::Span;
using rb::storage::build_image_path;
using rb::storage::kMaxImagePath;

namespace {

std::array<char, kMaxImagePath + 1> g_path{};

rb::core::Result<std::size_t> build(const char* base, const char* location)
{
    g_path.fill('#');
    return build_image_path(base, ImageLocation::from(location).value(), Span<char>{g_path});
}

void assert_rejected(const char* location)
{
    const auto result = build("/sd", location);
    TEST_ASSERT_TRUE_MESSAGE(!result.ok() && result.error() == ErrorCode::InvalidLocation, location);
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_joins_base_and_location(void)
{
    const auto result = build("/sd", "dos25.atr");
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_STRING("/sd/dos25.atr", g_path.data());
    TEST_ASSERT_EQUAL_size_t(std::strlen("/sd/dos25.atr"), result.value());
}

static void test_accepts_subdirectories_and_spaces(void)
{
    TEST_ASSERT_TRUE(build("/sd", "games/Star Raiders.atr").ok());
    TEST_ASSERT_EQUAL_STRING("/sd/games/Star Raiders.atr", g_path.data());
}

static void test_base_with_trailing_separator(void)
{
    TEST_ASSERT_TRUE(build("/sd/", "a.atr").ok());
    TEST_ASSERT_EQUAL_STRING("/sd/a.atr", g_path.data());
}

static void test_rejects_absolute_and_platform_specific_paths(void)
{
    assert_rejected("/etc/passwd");
    assert_rejected("\\images\\a.atr");
    assert_rejected("sub\\a.atr");
    assert_rejected("C:a.atr");
}

static void test_rejects_dot_segments_and_empty_segments(void)
{
    assert_rejected("..");
    assert_rejected("../a.atr");
    assert_rejected("sub/../../a.atr");
    assert_rejected("./a.atr");
    assert_rejected("sub/.");
    assert_rejected("sub//a.atr");
    assert_rejected("sub/");
}

static void test_allows_dots_inside_names(void)
{
    TEST_ASSERT_TRUE(build("/sd", "..hidden").ok());
    TEST_ASSERT_TRUE(build("/sd", "v1.2/a..b.atr").ok());
}

static void test_rejects_paths_that_do_not_fit(void)
{
    std::array<char, 8> small{};
    const auto result =
        build_image_path("/sd", ImageLocation::from("long-name.atr").value(), Span<char>{small});
    TEST_ASSERT_TRUE(result.error() == ErrorCode::InvalidLocation);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_joins_base_and_location);
    RUN_TEST(test_accepts_subdirectories_and_spaces);
    RUN_TEST(test_base_with_trailing_separator);
    RUN_TEST(test_rejects_absolute_and_platform_specific_paths);
    RUN_TEST(test_rejects_dot_segments_and_empty_segments);
    RUN_TEST(test_allows_dots_inside_names);
    RUN_TEST(test_rejects_paths_that_do_not_fit);
    return UNITY_END();
}
