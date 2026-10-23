// Span<T>: non-owning view used to pass buffers without copying or allocating.

#include <array>
#include <cstdint>

#include "rb/core/span.hpp"
#include "unity.h"

using rb::core::Span;

void setUp(void) {}
void tearDown(void) {}

static void test_views_array_without_copy(void)
{
    std::array<uint8_t, 4> buffer{1, 2, 3, 4};
    Span<uint8_t> view{buffer};
    TEST_ASSERT_EQUAL_size_t(4, view.size());
    TEST_ASSERT_EQUAL_PTR(buffer.data(), view.data());
    view[0] = 9;
    TEST_ASSERT_EQUAL_UINT8(9, buffer[0]);
}

static void test_converts_to_const_view(void)
{
    std::array<uint8_t, 2> buffer{5, 6};
    Span<uint8_t> mutable_view{buffer};
    Span<const uint8_t> const_view = mutable_view;
    TEST_ASSERT_EQUAL_UINT8(6, const_view[1]);
}

static void test_first_and_subspan(void)
{
    uint8_t raw[6] = {0, 1, 2, 3, 4, 5};
    Span<uint8_t> view{raw};
    TEST_ASSERT_EQUAL_size_t(6, view.size());

    Span<uint8_t> head = view.first(2);
    TEST_ASSERT_EQUAL_size_t(2, head.size());
    TEST_ASSERT_EQUAL_UINT8(1, head[1]);

    Span<uint8_t> middle = view.subspan(2, 3);
    TEST_ASSERT_EQUAL_size_t(3, middle.size());
    TEST_ASSERT_EQUAL_UINT8(2, middle[0]);
    TEST_ASSERT_EQUAL_UINT8(4, middle[2]);
}

static void test_empty_span(void)
{
    Span<const uint8_t> empty;
    TEST_ASSERT_TRUE(empty.empty());
    TEST_ASSERT_EQUAL_size_t(0, empty.size());
    TEST_ASSERT_EQUAL_PTR(empty.begin(), empty.end());
}

static void test_iteration(void)
{
    const std::array<uint8_t, 3> buffer{1, 2, 3};
    unsigned sum = 0;
    for (uint8_t byte : Span<const uint8_t>{buffer}) {
        sum += byte;
    }
    TEST_ASSERT_EQUAL_UINT(6, sum);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_views_array_without_copy);
    RUN_TEST(test_converts_to_const_view);
    RUN_TEST(test_first_and_subspan);
    RUN_TEST(test_empty_span);
    RUN_TEST(test_iteration);
    return UNITY_END();
}
