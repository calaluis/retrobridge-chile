// FskWave (spec cassette-cas, C102, AC3): the signal of an FSK run has the same durations
// and alternating levels as the file, in portions of at most 32767 ticks. The whole-tape
// check uses the real International Karate from RB_CAS_SAMPLES.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "rb/cassette/cas_index.hpp"
#include "rb/cassette/fsk_wave.hpp"
#include "unity.h"

using rb::cassette::CasIndex;
using rb::cassette::CasIndexResult;
using rb::cassette::CasSegment;
using rb::cassette::CasStatus;
using rb::cassette::FskPortion;
using rb::cassette::FskWave;
using rb::cassette::MemoryTapeSource;
using rb::cassette::SegmentKind;
using rb::core::Span;

namespace {

using Bytes = std::vector<uint8_t>;

Bytes values(std::initializer_list<uint16_t> list)
{
    Bytes out;
    for (uint16_t v : list) {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>(v >> 8));
    }
    return out;
}

std::vector<FskPortion> drain(FskWave& wave)
{
    std::vector<FskPortion> out;
    FskPortion portion;
    while (wave.next(portion)) {
        out.push_back(portion);
    }
    return out;
}

void assertPortion(bool level, uint32_t ticks, const FskPortion& portion)
{
    TEST_ASSERT_EQUAL(level, portion.level);
    TEST_ASSERT_EQUAL_UINT32(ticks, portion.ticks);
}

bool readFile(const std::string& path, Bytes& out)
{
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    uint8_t buffer[4096];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof buffer, file)) > 0) {
        out.insert(out.end(), buffer, buffer + got);
    }
    std::fclose(file);
    return true;
}

CasSegment g_table[1024];

}  // namespace

void setUp() {}
void tearDown() {}

// Index 0 is space, 1 is mark, and so on; 1 unit = 100 ticks.
void test_levels_alternate_from_space()
{
    const Bytes piece = values({3, 5, 7});
    FskWave wave;
    wave.reset();
    wave.setValues(Span<const uint8_t>(piece.data(), piece.size()));
    const std::vector<FskPortion> out = drain(wave);
    TEST_ASSERT_EQUAL_size_t(3, out.size());
    assertPortion(false, 300, out[0]);
    assertPortion(true, 500, out[1]);
    assertPortion(false, 700, out[2]);
    TEST_ASSERT_EQUAL_UINT64(3, wave.valuesDone());
}

// A value of 0 gives no portion but uses its index: the next value keeps the parity.
void test_zero_value_skips_but_keeps_parity()
{
    const Bytes piece = values({2, 0, 4});
    FskWave wave;
    wave.reset();
    wave.setValues(Span<const uint8_t>(piece.data(), piece.size()));
    const std::vector<FskPortion> out = drain(wave);
    TEST_ASSERT_EQUAL_size_t(2, out.size());
    assertPortion(false, 200, out[0]);
    assertPortion(false, 400, out[1]);  // index 2: space again
    TEST_ASSERT_EQUAL_UINT64(3, wave.valuesDone());
}

// 400 units = 40000 ticks: two portions of the same level, 32767 + 7233.
void test_long_value_splits_into_portions()
{
    const Bytes piece = values({400, 1});
    FskWave wave;
    wave.reset();
    wave.setValues(Span<const uint8_t>(piece.data(), piece.size()));
    const std::vector<FskPortion> out = drain(wave);
    TEST_ASSERT_EQUAL_size_t(3, out.size());
    assertPortion(false, 32767, out[0]);
    assertPortion(false, 7233, out[1]);
    assertPortion(true, 100, out[2]);
}

// The largest value: 65535 units = 6553500 ticks, all portions within the slot.
void test_largest_value_sums_exactly()
{
    const Bytes piece = values({65535});
    FskWave wave;
    wave.reset();
    wave.setValues(Span<const uint8_t>(piece.data(), piece.size()));
    uint64_t total = 0;
    for (const FskPortion& p : drain(wave)) {
        TEST_ASSERT_TRUE(p.ticks > 0 && p.ticks <= FskWave::kMaxPortionTicks);
        TEST_ASSERT_FALSE(p.level);
        total += p.ticks;
    }
    TEST_ASSERT_EQUAL_UINT64(6553500, total);
    TEST_ASSERT_EQUAL_UINT64(6553500, FskWave::sumTicks(Span<const uint8_t>(piece.data(), piece.size())));
}

// Pieces of one run keep the parity; reset() starts again at space.
void test_parity_continues_across_pieces_until_reset()
{
    const Bytes first = values({1});
    const Bytes second = values({2, 3});
    FskWave wave;
    wave.reset();
    wave.setValues(Span<const uint8_t>(first.data(), first.size()));
    TEST_ASSERT_EQUAL_size_t(1, drain(wave).size());
    wave.setValues(Span<const uint8_t>(second.data(), second.size()));
    std::vector<FskPortion> out = drain(wave);
    TEST_ASSERT_EQUAL_size_t(2, out.size());
    assertPortion(true, 200, out[0]);
    assertPortion(false, 300, out[1]);
    wave.reset();
    wave.setValues(Span<const uint8_t>(second.data(), second.size()));
    out = drain(wave);
    assertPortion(false, 200, out[0]);
}

// A split remainder left when the next piece arrives plays first, at its own level.
void test_pending_remainder_plays_before_the_next_piece()
{
    const Bytes first = values({400});
    const Bytes second = values({2});
    FskWave wave;
    wave.reset();
    wave.setValues(Span<const uint8_t>(first.data(), first.size()));
    FskPortion portion;
    TEST_ASSERT_TRUE(wave.next(portion));
    assertPortion(false, 32767, portion);
    wave.setValues(Span<const uint8_t>(second.data(), second.size()));
    const std::vector<FskPortion> out = drain(wave);
    TEST_ASSERT_EQUAL_size_t(2, out.size());
    assertPortion(false, 7233, out[0]);
    assertPortion(true, 200, out[1]);
}

// reset() in the middle of a split drops the remainder.
void test_reset_drops_a_pending_remainder()
{
    const Bytes first = values({400});
    const Bytes second = values({3});
    FskWave wave;
    wave.reset();
    wave.setValues(Span<const uint8_t>(first.data(), first.size()));
    FskPortion portion;
    TEST_ASSERT_TRUE(wave.next(portion));
    wave.reset();
    wave.setValues(Span<const uint8_t>(second.data(), second.size()));
    const std::vector<FskPortion> out = drain(wave);
    TEST_ASSERT_EQUAL_size_t(1, out.size());
    assertPortion(false, 300, out[0]);
}

void test_empty_piece_gives_nothing()
{
    FskWave wave;
    wave.reset();
    wave.setValues(Span<const uint8_t>());
    FskPortion portion;
    TEST_ASSERT_FALSE(wave.next(portion));
    TEST_ASSERT_EQUAL_UINT64(0, FskWave::sumTicks(Span<const uint8_t>()));
}

// AC3 on a real tape: every run, chunk by chunk, gives a signal whose length equals the
// file's durations and whose levels follow the value index. False if the tape is missing.
bool checkRealTape(const char* name)
{
    const char* samples = std::getenv("RB_CAS_SAMPLES");
    if (samples == nullptr) {
        return false;
    }
    Bytes file;
    if (!readFile(std::string(samples) + "/" + name, file)) {
        return false;
    }
    MemoryTapeSource source(Span<const uint8_t>(file.data(), file.size()));
    const CasIndexResult index = CasIndex::build(source, Span<CasSegment>(g_table, 1024));
    TEST_ASSERT_EQUAL(static_cast<int>(CasStatus::Ok), static_cast<int>(index.status));

    uint32_t runs = 0;
    uint32_t splits = 0;
    uint64_t allTicks = 0;
    for (uint32_t s = 0; s < index.count; ++s) {
        const CasSegment& run = g_table[s];
        if (run.kind != SegmentKind::Fsk) {
            continue;
        }
        ++runs;
        FskWave wave;
        wave.reset();
        uint32_t at = run.offset;
        uint64_t expected = 0;
        uint64_t played = 0;
        uint64_t valueIndex = 0;
        for (uint8_t c = 0; c < run.chunks; ++c) {
            const uint16_t length = static_cast<uint16_t>(file[at + 4] | (file[at + 5] << 8));
            const Span<const uint8_t> piece(file.data() + at + 8, length);
            expected += FskWave::sumTicks(piece);
            wave.setValues(piece);
            // The expected signal, built straight from the file: per value, its level by the
            // run index, its ticks in slots of at most 32767; zeros give nothing.
            for (uint32_t i = 0; i < length / 2u; ++i, ++valueIndex) {
                uint32_t ticks = static_cast<uint32_t>(piece[2 * i] | (piece[2 * i + 1] << 8)) * 100u;
                while (ticks > 0) {
                    const uint32_t slot = ticks > 32767u ? 32767u : ticks;
                    FskPortion portion;
                    TEST_ASSERT_TRUE(wave.next(portion));
                    TEST_ASSERT_EQUAL((valueIndex & 1u) != 0, portion.level);
                    TEST_ASSERT_EQUAL_UINT32(slot, portion.ticks);
                    played += portion.ticks;
                    splits += ticks > 32767u ? 1u : 0u;
                    ticks -= slot;
                }
            }
            FskPortion extra;
            TEST_ASSERT_FALSE(wave.next(extra));
            at += 8u + length;
        }
        TEST_ASSERT_EQUAL_UINT64(expected, played);
        TEST_ASSERT_EQUAL_UINT64(valueIndex, wave.valuesDone());
        allTicks += played;
    }
    TEST_ASSERT_TRUE(runs > 0);
    TEST_ASSERT_TRUE(allTicks > 0);
    std::printf("%s: %u fsk runs, %u split portions, signal %llu us\n", name, static_cast<unsigned>(runs),
                static_cast<unsigned>(splits), static_cast<unsigned long long>(allTicks));
    return true;
}

void test_real_international_karate_runs()
{
    if (!checkRealTape("international_karate.cas")) {
        TEST_IGNORE_MESSAGE("RB_CAS_SAMPLES or international_karate.cas missing");
    }
}

// A turbo software tape made of fsk only: long runs, values long enough to split.
void test_real_turbo_software_runs()
{
    if (!checkRealTape("turbo_software_zorro.cas")) {
        TEST_IGNORE_MESSAGE("RB_CAS_SAMPLES or turbo_software_zorro.cas missing");
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_levels_alternate_from_space);
    RUN_TEST(test_zero_value_skips_but_keeps_parity);
    RUN_TEST(test_long_value_splits_into_portions);
    RUN_TEST(test_largest_value_sums_exactly);
    RUN_TEST(test_parity_continues_across_pieces_until_reset);
    RUN_TEST(test_pending_remainder_plays_before_the_next_piece);
    RUN_TEST(test_reset_drops_a_pending_remainder);
    RUN_TEST(test_empty_piece_gives_nothing);
    RUN_TEST(test_real_international_karate_runs);
    RUN_TEST(test_real_turbo_software_runs);
    return UNITY_END();
}
