#pragma once

// TapeSource (spec cassette-cas, C101): where the cassette reads a .cas from. Positional
// reads, so no file cursor is shared: the index reads chunk headers, the player and the
// FSK loader read payloads. On the ESP32 it is a file on the SD card; on the host, a file
// or memory.

#include <cstdint>

#include "rb/core/span.hpp"

namespace rb::cassette {

class TapeSource {
public:
    virtual ~TapeSource() = default;
    // Total bytes of the .cas.
    virtual uint32_t size() const = 0;
    // Fills all of `dst` from `offset`. False on a short read or an I/O error.
    virtual bool read(uint32_t offset, rb::core::Span<uint8_t> dst) = 0;
};

// A .cas already in memory (host tests and tools). Does not own the bytes.
class MemoryTapeSource final : public TapeSource {
public:
    explicit MemoryTapeSource(rb::core::Span<const uint8_t> bytes) noexcept : bytes_(bytes) {}

    uint32_t size() const override { return static_cast<uint32_t>(bytes_.size()); }

    bool read(uint32_t offset, rb::core::Span<uint8_t> dst) override
    {
        if (offset > bytes_.size() || dst.size() > bytes_.size() - offset) {
            return false;
        }
        for (std::size_t i = 0; i < dst.size(); ++i) {
            dst[i] = bytes_[offset + i];
        }
        return true;
    }

private:
    rb::core::Span<const uint8_t> bytes_;
};

}  // namespace rb::cassette
