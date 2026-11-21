// SHA-256 from FIPS 180-4 (see sha256.hpp). Section numbers refer to it.
#include "rb/menu/sha256.hpp"

#include "rb/core/assert.hpp"

namespace rb::host {

namespace {

// 4.2.2: the first 32 bits of the fractional parts of the cube roots of the
// first sixty-four primes.
constexpr std::array<uint32_t, 64> kK = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

// 5.3.3: initial hash value H(0).
constexpr std::array<uint32_t, 8> kInitial = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

// 3.2 and 4.1.2.
constexpr uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
constexpr uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
constexpr uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
constexpr uint32_t bigSigma0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
constexpr uint32_t bigSigma1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
constexpr uint32_t smallSigma0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
constexpr uint32_t smallSigma1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

}  // namespace

void Sha256::reset() noexcept
{
    state_ = kInitial;
    blockUsed_ = 0;
    totalBytes_ = 0;
    finished_ = false;
}

// 6.2.2, steps 1 to 4, for one 512-bit block. Words are big-endian (3.1, 5.2.1).
void Sha256::compress(const uint8_t* block) noexcept
{
    std::array<uint32_t, 64> w{};
    for (unsigned t = 0; t < 16; ++t) {
        w[t] = (uint32_t{block[4 * t]} << 24) | (uint32_t{block[4 * t + 1]} << 16) |
               (uint32_t{block[4 * t + 2]} << 8) | uint32_t{block[4 * t + 3]};
    }
    for (unsigned t = 16; t < 64; ++t) {
        w[t] = smallSigma1(w[t - 2]) + w[t - 7] + smallSigma0(w[t - 15]) + w[t - 16];
    }

    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (unsigned t = 0; t < 64; ++t) {
        const uint32_t t1 = h + bigSigma1(e) + ch(e, f, g) + kK[t] + w[t];
        const uint32_t t2 = bigSigma0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(rb::core::Span<const uint8_t> bytes) noexcept
{
    RB_ASSERT(!finished_);
    totalBytes_ += bytes.size();
    std::size_t at = 0;
    while (at < bytes.size()) {
        const std::size_t room = block_.size() - blockUsed_;
        const std::size_t take = bytes.size() - at < room ? bytes.size() - at : room;
        for (std::size_t i = 0; i < take; ++i) {
            block_[blockUsed_ + i] = bytes[at + i];
        }
        blockUsed_ += take;
        at += take;
        if (blockUsed_ == block_.size()) {
            compress(block_.data());
            blockUsed_ = 0;
        }
    }
}

// 5.1.1: a "1" bit, zeros up to 448 mod 512, then the bit length as 64 bits.
Sha256Digest Sha256::finish() noexcept
{
    RB_ASSERT(!finished_);
    finished_ = true;
    const uint64_t bitLength = totalBytes_ * 8u;
    block_[blockUsed_++] = 0x80;
    if (blockUsed_ > 56) {  // no room left for the length: it goes in one more block
        while (blockUsed_ < 64) {
            block_[blockUsed_++] = 0x00;
        }
        compress(block_.data());
        blockUsed_ = 0;
    }
    while (blockUsed_ < 56) {
        block_[blockUsed_++] = 0x00;
    }
    for (unsigned i = 0; i < 8; ++i) {
        block_[56 + i] = static_cast<uint8_t>(bitLength >> (56 - 8 * i));
    }
    compress(block_.data());
    blockUsed_ = 0;

    // 6.2.2: H(N) = H0 || H1 || ... || H7, each word big-endian.
    Sha256Digest digest{};
    for (unsigned i = 0; i < 8; ++i) {
        digest[4 * i] = static_cast<uint8_t>(state_[i] >> 24);
        digest[4 * i + 1] = static_cast<uint8_t>(state_[i] >> 16);
        digest[4 * i + 2] = static_cast<uint8_t>(state_[i] >> 8);
        digest[4 * i + 3] = static_cast<uint8_t>(state_[i]);
    }
    return digest;
}

Sha256Digest sha256(rb::core::Span<const uint8_t> bytes) noexcept
{
    Sha256 hasher;
    hasher.update(bytes);
    return hasher.finish();
}

std::array<char, 65> toHex(const Sha256Digest& digest) noexcept
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::array<char, 65> text{};
    for (std::size_t i = 0; i < digest.size(); ++i) {
        text[2 * i] = kDigits[digest[i] >> 4];
        text[2 * i + 1] = kDigits[digest[i] & 0x0F];
    }
    text[64] = '\0';
    return text;
}

}  // namespace rb::host
