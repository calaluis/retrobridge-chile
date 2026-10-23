#pragma once

#include <array>
#include <cstddef>
#include <type_traits>

#include "rb/core/assert.hpp"

namespace rb::core {

// Non-owning view of a contiguous buffer (C++17 has no std::span).
// A Span must not be stored beyond the call that received it (ADR-011).
template <typename T>
class Span {
public:
    constexpr Span() noexcept = default;
    constexpr Span(T* data, std::size_t size) noexcept : data_(data), size_(size) {}

    template <std::size_t N>
    constexpr Span(T (&array)[N]) noexcept : data_(array), size_(N)
    {
    }

    template <typename U, std::size_t N,
              typename = std::enable_if_t<std::is_convertible<U (*)[], T (*)[]>::value>>
    constexpr Span(std::array<U, N>& array) noexcept : data_(array.data()), size_(N)
    {
    }

    template <typename U, std::size_t N,
              typename = std::enable_if_t<std::is_convertible<const U (*)[], T (*)[]>::value>>
    constexpr Span(const std::array<U, N>& array) noexcept : data_(array.data()), size_(N)
    {
    }

    // Span<T> -> Span<const T>
    template <typename U, typename = std::enable_if_t<std::is_convertible<U (*)[], T (*)[]>::value>>
    constexpr Span(const Span<U>& other) noexcept : data_(other.data()), size_(other.size())
    {
    }

    constexpr T* data() const noexcept { return data_; }
    constexpr std::size_t size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }
    constexpr T* begin() const noexcept { return data_; }
    constexpr T* end() const noexcept { return data_ + size_; }

    T& operator[](std::size_t index) const
    {
        RB_ASSERT(index < size_);
        return data_[index];
    }

    Span first(std::size_t count) const
    {
        RB_ASSERT(count <= size_);
        return Span{data_, count};
    }

    Span subspan(std::size_t offset, std::size_t count) const
    {
        RB_ASSERT(offset <= size_ && count <= size_ - offset);
        return Span{data_ + offset, count};
    }

private:
    T* data_ = nullptr;
    std::size_t size_ = 0;
};

}  // namespace rb::core
