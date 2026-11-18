// SHA-256 for the host's log (menu-tnfs T004, ADR-012): the digest of every
// image loaded over TNFS, so a run can be compared with the reference copy.
// Written from FIPS 180-4 (docs/fuentes.md), sections 4.1.2, 4.2.2, 5.1.1,
// 5.2.1, 5.3.3 and 6.2. No other implementation was read.
//
// Byte-oriented: the message is a whole number of bytes. Messages up to 2^61-1
// bytes are supported (the 64-bit bit-length field of 5.1.1). No allocation.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "rb/core/span.hpp"

namespace rb::host {

using Sha256Digest = std::array<uint8_t, 32>;

// Incremental: update() any number of times with any split, then finish() once.
// After finish() the object must not be used again, except through reset():
// update() or finish() on a finished object stops the program (RB_ASSERT)
// instead of returning a wrong digest.
class Sha256 {
public:
    Sha256() noexcept { reset(); }

    void reset() noexcept;
    void update(rb::core::Span<const uint8_t> bytes) noexcept;
    Sha256Digest finish() noexcept;

private:
    void compress(const uint8_t* block) noexcept;

    std::array<uint32_t, 8> state_{};
    std::array<uint8_t, 64> block_{};
    std::size_t blockUsed_ = 0;
    uint64_t totalBytes_ = 0;
    bool finished_ = false;
};

Sha256Digest sha256(rb::core::Span<const uint8_t> bytes) noexcept;

// 64 lowercase hex characters and a NUL.
std::array<char, 65> toHex(const Sha256Digest& digest) noexcept;

}  // namespace rb::host
