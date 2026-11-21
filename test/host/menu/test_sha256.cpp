// SHA-256 (menu-tnfs T004). Expected digests: the four messages of the NIST
// SHA-256 examples that accompany FIPS 180-4 (empty, "abc", the 448-bit message
// and one million "a"), plus padding-boundary lengths and the reference ATR of
// the spec. Every expected value was also checked with two independent tools
// (Python hashlib and Windows certutil); none of them is used by this code.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "rb/menu/sha256.hpp"
#include "unity.h"

using rb::core::Span;
using rb::host::Sha256;
using rb::host::sha256;
using rb::host::toHex;

namespace {

void assert_hex(const char* expected, const rb::host::Sha256Digest& digest)
{
    TEST_ASSERT_EQUAL_STRING(expected, toHex(digest).data());
}

Span<const uint8_t> text(const char* s)
{
    return Span<const uint8_t>{reinterpret_cast<const uint8_t*>(s), std::strlen(s)};
}

std::vector<uint8_t> pattern(std::size_t n)
{
    std::vector<uint8_t> bytes(n);
    for (std::size_t i = 0; i < n; ++i) {
        bytes[i] = static_cast<uint8_t>((i * 31 + 7) & 0xFF);
    }
    return bytes;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_nist_examples(void)
{
    assert_hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", sha256(text("")));
    assert_hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", sha256(text("abc")));
    assert_hex("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
               sha256(text("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")));
}

static void test_one_million_a(void)
{
    std::vector<uint8_t> a(1000000, 'a');
    assert_hex("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
               sha256(Span<const uint8_t>{a.data(), a.size()}));
}

// 55 bytes still take one block; 56 to 63 need a second block for the length;
// 64, 119 and 120 cross whole-block boundaries.
static void test_padding_boundaries(void)
{
    struct Case {
        std::size_t length;
        const char* digest;
    };
    const Case cases[] = {
        {55, "8aa994584139d128848eeebc4e815639ba5ab6e6e39574195a63ac4f14f7c43b"},
        {56, "ad574708f75c044c9b85de64cb568ee7711ff4f36448c6242f053ba8f6cc2b63"},
        {63, "280ed3e8ff1df845b2e7dfe6ac6cee817bef20e783cc65abc41b818b4d2fe076"},
        {64, "c6ab9724ade5b6a7a1edfffb12f3aa9181351355af8fd08c919952ad211339dd"},
        {65, "788367c73c7ddf4c53f65e68cc0d943e6227ab55b0e78ba63ace822b1c6301c0"},
        {119, "3d610547d68216dedf7435a4fb6260353911f6b3fd3f18805ddb8be285d726fe"},
        {120, "1f80156a804cb7862ad113e8200e9d74499723e7c7854d5f48776d3148e09656"},
    };
    for (const Case& c : cases) {
        std::vector<uint8_t> bytes = pattern(c.length);
        assert_hex(c.digest, sha256(Span<const uint8_t>{bytes.data(), bytes.size()}));
    }
}

// Any split of the message into update() calls gives the same digest.
static void test_incremental_splits_match(void)
{
    std::vector<uint8_t> bytes = pattern(120);
    const char* expected = "1f80156a804cb7862ad113e8200e9d74499723e7c7854d5f48776d3148e09656";
    const std::size_t steps[] = {1, 3, 55, 63, 64, 65, 119};
    for (std::size_t step : steps) {
        Sha256 hasher;
        for (std::size_t at = 0; at < bytes.size(); at += step) {
            const std::size_t n = at + step <= bytes.size() ? step : bytes.size() - at;
            hasher.update(Span<const uint8_t>{bytes.data() + at, n});
        }
        hasher.update(Span<const uint8_t>{});  // an empty update changes nothing
        assert_hex(expected, hasher.finish());
    }

    Sha256 reused;
    reused.update(text("something else"));
    (void)reused.finish();
    reused.reset();
    reused.update(text("abc"));
    assert_hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", reused.finish());
}

// The reference image of the spec (Atari Logo.atr, 92 176 bytes). It is not in
// the repository; set RB_REF_ATR to its path to run this check.
static void test_reference_atr(void)
{
    const char* path = std::getenv("RB_REF_ATR");
    if (path == nullptr || path[0] == '\0') {
        TEST_IGNORE_MESSAGE("RB_REF_ATR not set: reference ATR not checked");
    }
    std::FILE* file = std::fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(file, "RB_REF_ATR cannot be opened");
    std::vector<uint8_t> bytes;
    uint8_t chunk[4096];
    std::size_t got = 0;
    while ((got = std::fread(chunk, 1, sizeof chunk, file)) > 0) {
        bytes.insert(bytes.end(), chunk, chunk + got);
    }
    std::fclose(file);
    TEST_ASSERT_EQUAL_size_t(92176, bytes.size());
    assert_hex("9f21ef9a3dbf9791e6d94975b2eed9ab3fff7feaa4e05c76c1f14dbbd4c99c96",
               sha256(Span<const uint8_t>{bytes.data(), bytes.size()}));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nist_examples);
    RUN_TEST(test_one_million_a);
    RUN_TEST(test_padding_boundaries);
    RUN_TEST(test_incremental_splits_match);
    RUN_TEST(test_reference_atr);
    return UNITY_END();
}
