#pragma once

#include <optional>
#include <utility>

#include "rb/core/assert.hpp"
#include "rb/core/error_code.hpp"

namespace rb::core {

// Error marker convertible to any Result: `return Failure{ErrorCode::X};`
struct Failure {
    ErrorCode code;
};

// Value or error, without exceptions (ADR-003). Storage is inline (std::optional),
// so no allocation; Result<T> is copyable/movable exactly when T is, which lets it
// carry move-only types such as DiskImage.
template <typename T>
class [[nodiscard]] Result {
public:
    Result(const T& value) : value_(value) {}
    Result(T&& value) : value_(std::move(value)) {}
    Result(Failure failure) : error_(failure.code) {}

    bool ok() const noexcept { return value_.has_value(); }

    ErrorCode error() const
    {
        RB_ASSERT(!ok());
        return error_;
    }

    const T& value() const&
    {
        RB_ASSERT(ok());
        return *value_;
    }

    T& value() &
    {
        RB_ASSERT(ok());
        return *value_;
    }

    T&& value() &&
    {
        RB_ASSERT(ok());
        return std::move(*value_);
    }

private:
    std::optional<T> value_;
    ErrorCode error_{};
};

template <>
class [[nodiscard]] Result<void> {
public:
    Result() noexcept = default;
    Result(Failure failure) noexcept : ok_(false), error_(failure.code) {}

    bool ok() const noexcept { return ok_; }

    ErrorCode error() const
    {
        RB_ASSERT(!ok_);
        return error_;
    }

private:
    bool ok_ = true;
    ErrorCode error_{};
};

}  // namespace rb::core
