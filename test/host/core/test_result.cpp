// Result<T> / Result<void> (ADR-003): value-or-error without exceptions.

#include <type_traits>
#include <utility>

#include "rb/core/result.hpp"
#include "unity.h"

using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;

namespace {

// Move-only payload, like DiskImage.
class MoveOnly {
public:
    explicit MoveOnly(int value) : value_(value) {}
    MoveOnly(MoveOnly&& other) noexcept : value_(other.value_) { other.value_ = -1; }
    MoveOnly& operator=(MoveOnly&& other) noexcept
    {
        value_ = other.value_;
        other.value_ = -1;
        return *this;
    }
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    int value() const { return value_; }

private:
    int value_;
};

Result<int> parse_positive(int input)
{
    if (input <= 0) {
        return Failure{ErrorCode::InvalidArgument};
    }
    return input;
}

Result<void> check_even(int input)
{
    if (input % 2 != 0) {
        return Failure{ErrorCode::InvalidArgument};
    }
    return {};
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_success_holds_value(void)
{
    const Result<int> result = parse_positive(7);
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_INT(7, result.value());
}

static void test_failure_holds_error(void)
{
    const Result<int> result = parse_positive(0);
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ErrorCode::InvalidArgument),
                            static_cast<uint8_t>(result.error()));
}

static void test_void_result(void)
{
    TEST_ASSERT_TRUE(check_even(4).ok());
    const Result<void> odd = check_even(3);
    TEST_ASSERT_FALSE(odd.ok());
    TEST_ASSERT_TRUE(odd.error() == ErrorCode::InvalidArgument);
}

static void test_move_only_payload(void)
{
    static_assert(!std::is_copy_constructible<Result<MoveOnly>>::value,
                  "Result of a move-only type must not be copyable");
    static_assert(std::is_move_constructible<Result<MoveOnly>>::value,
                  "Result of a move-only type must be movable");

    Result<MoveOnly> result{MoveOnly{42}};
    TEST_ASSERT_TRUE(result.ok());
    MoveOnly taken = std::move(result).value();
    TEST_ASSERT_EQUAL_INT(42, taken.value());
}

static void test_move_only_failure(void)
{
    Result<MoveOnly> result{Failure{ErrorCode::IoError}};
    TEST_ASSERT_FALSE(result.ok());
    TEST_ASSERT_TRUE(result.error() == ErrorCode::IoError);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_success_holds_value);
    RUN_TEST(test_failure_holds_error);
    RUN_TEST(test_void_result);
    RUN_TEST(test_move_only_payload);
    RUN_TEST(test_move_only_failure);
    return UNITY_END();
}
