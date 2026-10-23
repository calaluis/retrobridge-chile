// Span<T>: non-owning view used to pass buffers without copying or allocating.

#include <array>
#include <cstdint>
#include <cstring>

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

// --- Null data (a default-constructed Span has data() == nullptr) -------------

static void test_empty_span_has_null_begin_and_end(void)
{
    const Span<uint8_t> empty;
    TEST_ASSERT_NULL(empty.data());
    TEST_ASSERT_NULL(empty.begin());
    TEST_ASSERT_NULL(empty.end());
}

static void test_explicit_null_with_zero_size_is_empty(void)
{
    const Span<const uint8_t> empty{nullptr, 0};
    TEST_ASSERT_TRUE(empty.empty());
    TEST_ASSERT_NULL(empty.end());
}

static void test_iterating_an_empty_span_visits_nothing(void)
{
    unsigned visits = 0;
    for (uint8_t byte : Span<const uint8_t>{}) {
        (void)byte;
        ++visits;
    }
    TEST_ASSERT_EQUAL_UINT(0, visits);
}

static void test_subspan_and_first_of_empty_span(void)
{
    const Span<uint8_t> empty;
    const Span<uint8_t> sub = empty.subspan(0, 0);
    TEST_ASSERT_TRUE(sub.empty());
    TEST_ASSERT_NULL(sub.data());
    TEST_ASSERT_NULL(sub.end());

    const Span<uint8_t> head = empty.first(0);
    TEST_ASSERT_TRUE(head.empty());
    TEST_ASSERT_NULL(head.data());
}

static void test_empty_subspan_at_end_of_buffer(void)
{
    uint8_t raw[4] = {0, 1, 2, 3};
    const Span<uint8_t> view{raw};
    const Span<uint8_t> tail = view.subspan(4, 0);
    TEST_ASSERT_TRUE(tail.empty());
    TEST_ASSERT_EQUAL_PTR(raw + 4, tail.data());
}

// A null pointer with a non-zero size violates the Span invariant: RB_ASSERT
// must abort. Unity cannot observe an abort in-process, so CTest runs this
// binary a second time with the argument below and expects it to fail
// (test `test_span_rejects_null_with_size`, WILL_FAIL).
static int construct_null_with_size(void)
{
    const Span<const uint8_t> invalid{nullptr, 1};
    return invalid.size() == 1 ? 0 : 1;  // reached only if the assertion did not fire
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--construct-null-with-size") == 0) {
        return construct_null_with_size();
    }

    UNITY_BEGIN();
    RUN_TEST(test_views_array_without_copy);
    RUN_TEST(test_converts_to_const_view);
    RUN_TEST(test_first_and_subspan);
    RUN_TEST(test_empty_span);
    RUN_TEST(test_iteration);
    RUN_TEST(test_empty_span_has_null_begin_and_end);
    RUN_TEST(test_explicit_null_with_zero_size_is_empty);
    RUN_TEST(test_iterating_an_empty_span_visits_nothing);
    RUN_TEST(test_subspan_and_first_of_empty_span);
    RUN_TEST(test_empty_subspan_at_end_of_buffer);
    return UNITY_END();
}
