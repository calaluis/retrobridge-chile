#pragma once

// Contract suite for the ImageByteSource port (architecture §5.1, §10.3).
// Every implementation (test doubles now, FATFS in H3, TNFS later) must pass it.

#include <array>
#include <cstdint>

#include "rb/domain/image_byte_source.hpp"
#include "unity.h"

namespace rb::test {

using ExpectedByte = uint8_t (*)(uint32_t offset);

// `source` must hold at least 32 bytes; `expected` gives the byte at any offset.
inline void check_image_byte_source_contract(rb::domain::ImageByteSource& source, ExpectedByte expected)
{
    using rb::core::ErrorCode;
    using rb::core::Span;

    const uint32_t size = source.size();
    TEST_ASSERT_TRUE_MESSAGE(size >= 32, "contract needs a source of at least 32 bytes");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(size, source.size(), "size() must be stable");

    // Content at the start, in the middle and at the very end.
    std::array<uint8_t, 8> buffer{};
    const uint32_t offsets[] = {0, size / 2, size - 8};
    for (uint32_t offset : offsets) {
        TEST_ASSERT_TRUE(source.readAt(offset, Span<uint8_t>{buffer}).ok());
        for (uint32_t i = 0; i < buffer.size(); ++i) {
            TEST_ASSERT_EQUAL_UINT8(expected(offset + i), buffer[i]);
        }
    }

    // Zero-length reads inside the image, including exactly at the end.
    TEST_ASSERT_TRUE(source.readAt(0, Span<uint8_t>{}).ok());
    TEST_ASSERT_TRUE(source.readAt(size, Span<uint8_t>{}).ok());

    // All-or-nothing: a read crossing the end fails and writes nothing.
    buffer.fill(0xA5);
    rb::core::Result<void> crossing = source.readAt(size - 4, Span<uint8_t>{buffer});
    TEST_ASSERT_TRUE(crossing.error() == ErrorCode::OutOfBounds);
    for (uint8_t byte : buffer) {
        TEST_ASSERT_EQUAL_UINT8(0xA5, byte);
    }

    // Offsets past the end, including ones that would overflow offset + length.
    TEST_ASSERT_TRUE(source.readAt(size + 1, Span<uint8_t>{}).error() == ErrorCode::OutOfBounds);
    TEST_ASSERT_TRUE(source.readAt(0xFFFFFFFFu, Span<uint8_t>{buffer}).error() == ErrorCode::OutOfBounds);

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(size, source.size(), "size() must be stable after reads");
}

}  // namespace rb::test
