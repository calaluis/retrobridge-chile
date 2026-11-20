// CasImage::index (spec cassette-cas, C001): which .cas files are level 1, which are
// pulse tapes (type B), which are broken, and the blocks the cassette plays.

#include <cstdint>
#include <string>
#include <vector>

#include "cas_image.hpp"
#include "unity.h"

using rb::core::Span;
using rb::host::CasBlock;
using rb::host::CasImage;
using rb::host::CasResult;
using rb::host::CasTape;

namespace {

using Bytes = std::vector<uint8_t>;

Bytes chunk(const char* type, uint16_t aux, const Bytes& data)
{
    Bytes out(type, type + 4);
    out.push_back(static_cast<uint8_t>(data.size() & 0xFF));
    out.push_back(static_cast<uint8_t>(data.size() >> 8));
    out.push_back(static_cast<uint8_t>(aux & 0xFF));
    out.push_back(static_cast<uint8_t>(aux >> 8));
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

Bytes operator+(Bytes a, const Bytes& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

Bytes fuji() { return chunk("FUJI", 0, {'T', 'E', 'S', 'T'}); }
Bytes baud(uint16_t rate) { return chunk("baud", rate, {}); }
Bytes data(uint16_t gapMs, const Bytes& bytes) { return chunk("data", gapMs, bytes); }
Bytes record(uint8_t fill) { return Bytes(132, fill); }

CasResult index(const Bytes& file, CasTape& tape)
{
    return CasImage::index(Span<const uint8_t>(file.data(), file.size()), tape);
}

}  // namespace

void setUp() {}
void tearDown() {}

// A standard tape: no baud chunk means 600 baud; each data block keeps its gap and bytes.
void test_standard_tape_defaults_to_600_baud()
{
    const Bytes file = fuji() + data(20000, record(0x55)) + data(250, record(0xAA));
    CasTape tape;
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Ok), static_cast<int>(index(file, tape)));
    TEST_ASSERT_EQUAL_UINT32(2, tape.blocks.size());
    const CasBlock& first = tape.blocks[0];
    TEST_ASSERT_TRUE(first.isData);
    TEST_ASSERT_EQUAL_UINT32(600, first.baud);
    TEST_ASSERT_EQUAL_UINT32(20000, first.gapMs);
    TEST_ASSERT_EQUAL_UINT32(132, first.length);
    TEST_ASSERT_EQUAL_UINT8(0x55, file[first.offset]);
    TEST_ASSERT_EQUAL_UINT8(0x55, file[first.offset + 131]);
    TEST_ASSERT_EQUAL_UINT32(250, tape.blocks[1].gapMs);
    TEST_ASSERT_EQUAL_UINT8(0xAA, file[tape.blocks[1].offset]);
}

// AC6: a baud chunk sets the rate of the data blocks after it, until the next one.
void test_baud_chunks_set_the_rate_of_following_blocks()
{
    const Bytes file = fuji() + baud(600) + data(100, record(1)) + baud(800) + data(0, record(2)) +
                       data(5, record(3));
    CasTape tape;
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Ok), static_cast<int>(index(file, tape)));
    TEST_ASSERT_EQUAL_UINT32(3, tape.blocks.size());
    TEST_ASSERT_EQUAL_UINT32(600, tape.blocks[0].baud);
    TEST_ASSERT_EQUAL_UINT32(800, tape.blocks[1].baud);
    TEST_ASSERT_EQUAL_UINT32(800, tape.blocks[2].baud);
    TEST_ASSERT_EQUAL_UINT32(5, tape.blocks[2].gapMs);
}

// An fsk chunk in a level 1 tape plays as silence: its gap plus its durations (1/10 ms each).
void test_fsk_chunk_in_level_1_tape_is_silence()
{
    const Bytes pulses = {0x0A, 0x00, 0x14, 0x00};  // 10 + 20 tenths of a millisecond = 3 ms
    const Bytes file = fuji() + data(100, record(1)) + chunk("fsk ", 7, pulses) + data(0, record(2));
    CasTape tape;
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Ok), static_cast<int>(index(file, tape)));
    TEST_ASSERT_EQUAL_UINT32(3, tape.blocks.size());
    const CasBlock& silence = tape.blocks[1];
    TEST_ASSERT_FALSE(silence.isData);
    TEST_ASSERT_EQUAL_UINT32(7, silence.gapMs);
    TEST_ASSERT_EQUAL_UINT32(3000, silence.silenceUs);
    TEST_ASSERT_EQUAL_UINT32(0, silence.length);
}

// Unknown chunk types (A8CAS labels, later FUJI chunks) are skipped.
void test_unknown_chunks_are_skipped()
{
    const Bytes file = fuji() + chunk("zzzz", 3, {1, 2, 3}) + fuji() + data(0, record(9));
    CasTape tape;
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Ok), static_cast<int>(index(file, tape)));
    TEST_ASSERT_EQUAL_UINT32(1, tape.blocks.size());
    TEST_ASSERT_EQUAL_UINT8(9, file[tape.blocks[0].offset]);
}

// AC8: data only in fsk chunks is a pulse tape (type B).
void test_fsk_only_tape_is_pulses()
{
    const Bytes file = fuji() + baud(600) + chunk("fsk ", 0, {0x10, 0x00, 0x10, 0x00}) +
                       chunk("fsk ", 999, {0x20, 0x00});
    CasTape tape;
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Pulses), static_cast<int>(index(file, tape)));
    TEST_ASSERT_EQUAL_UINT32(0, tape.blocks.size());
}

// AC8: PWM chunks are pulses too; level 1 cannot play them even next to data blocks.
void test_pwm_chunks_are_pulses()
{
    const Bytes file = fuji() + data(0, record(1)) + chunk("pwms", 0, {0x44, 0xAC}) +
                       chunk("pwmd", 0x0402, {1, 2, 3});
    CasTape tape;
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Pulses), static_cast<int>(index(file, tape)));
}

// AC9: no FUJI chunk at the start.
void test_missing_fuji_is_invalid()
{
    const Bytes file = data(0, record(1));
    CasTape tape;
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Invalid), static_cast<int>(index(file, tape)));
}

// AC9: a chunk whose length runs past the end of the file.
void test_cut_chunk_is_invalid()
{
    Bytes file = fuji() + data(0, record(1));
    file.resize(file.size() - 1);
    CasTape tape;
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Invalid), static_cast<int>(index(file, tape)));
    TEST_ASSERT_EQUAL_UINT32(0, tape.blocks.size());
}

// AC9: a header cut short (fewer than 8 bytes left).
void test_cut_header_is_invalid()
{
    const Bytes file = fuji() + data(0, record(1)) + Bytes{'d', 'a', 't'};
    CasTape tape;
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Invalid), static_cast<int>(index(file, tape)));
}

// AC9: an fsk chunk with an odd length cannot hold whole 16-bit durations.
void test_odd_fsk_length_is_invalid()
{
    const Bytes file = fuji() + data(0, record(1)) + chunk("fsk ", 0, {1, 2, 3});
    CasTape tape;
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Invalid), static_cast<int>(index(file, tape)));
    TEST_ASSERT_EQUAL_UINT32(0, tape.blocks.size());
}

// AC9: a tape with nothing to play is not a valid tape.
void test_tape_without_blocks_is_invalid()
{
    const Bytes file = fuji() + baud(600);
    CasTape tape;
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Invalid), static_cast<int>(index(file, tape)));
}

// A failed index leaves no blocks behind, even if `tape` held some before.
void test_failure_clears_the_tape()
{
    CasTape tape;
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Ok), static_cast<int>(index(fuji() + data(0, record(1)), tape)));
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Invalid), static_cast<int>(index(data(0, record(1)), tape)));
    TEST_ASSERT_EQUAL_UINT32(0, tape.blocks.size());
}

// AC9: a baud chunk with rate 0 cannot be played.
void test_baud_zero_is_invalid()
{
    const Bytes file = fuji() + baud(0) + data(0, record(1));
    CasTape tape;
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Invalid), static_cast<int>(index(file, tape)));
    TEST_ASSERT_EQUAL_UINT32(0, tape.blocks.size());
}

// AC9: a FUJI chunk alone, or empty data records, leave nothing to play.
void test_fuji_only_and_empty_records_are_invalid()
{
    CasTape tape;
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Invalid), static_cast<int>(index(fuji(), tape)));
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Invalid),
                      static_cast<int>(index(fuji() + data(100, {}), tape)));
    TEST_ASSERT_EQUAL_UINT32(0, tape.blocks.size());
}

// AC8: an empty data record next to fsk pulses does not make the tape level 1.
void test_empty_record_with_fsk_is_pulses()
{
    const Bytes file = fuji() + data(0, {}) + chunk("fsk ", 0, {0x10, 0x00});
    CasTape tape;
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Pulses), static_cast<int>(index(file, tape)));
}

// An empty data record keeps its gap as silence; an empty fsk chunk is only its gap.
void test_empty_chunks_keep_their_gap()
{
    const Bytes file = fuji() + data(40, {}) + chunk("fsk ", 9, {}) + data(0, record(1));
    CasTape tape;
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Ok), static_cast<int>(index(file, tape)));
    TEST_ASSERT_EQUAL_UINT32(3, tape.blocks.size());
    TEST_ASSERT_FALSE(tape.blocks[0].isData);
    TEST_ASSERT_EQUAL_UINT32(40, tape.blocks[0].gapMs);
    TEST_ASSERT_FALSE(tape.blocks[1].isData);
    TEST_ASSERT_EQUAL_UINT32(9, tape.blocks[1].gapMs);
    TEST_ASSERT_TRUE(tape.blocks[1].silenceUs == 0);
}

// A long fsk silence does not wrap: 656 durations of 0xFFFF are 4,299,096,000 us.
void test_long_fsk_silence_does_not_wrap()
{
    Bytes pulses;
    for (int i = 0; i < 656; ++i) {
        pulses.push_back(0xFF);
        pulses.push_back(0xFF);
    }
    const Bytes file = fuji() + data(0, record(1)) + chunk("fsk ", 0, pulses);
    CasTape tape;
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Ok), static_cast<int>(index(file, tape)));
    TEST_ASSERT_TRUE(tape.blocks[1].silenceUs == 656ull * 65535ull * 100ull);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_standard_tape_defaults_to_600_baud);
    RUN_TEST(test_baud_chunks_set_the_rate_of_following_blocks);
    RUN_TEST(test_fsk_chunk_in_level_1_tape_is_silence);
    RUN_TEST(test_unknown_chunks_are_skipped);
    RUN_TEST(test_fsk_only_tape_is_pulses);
    RUN_TEST(test_pwm_chunks_are_pulses);
    RUN_TEST(test_missing_fuji_is_invalid);
    RUN_TEST(test_cut_chunk_is_invalid);
    RUN_TEST(test_cut_header_is_invalid);
    RUN_TEST(test_odd_fsk_length_is_invalid);
    RUN_TEST(test_tape_without_blocks_is_invalid);
    RUN_TEST(test_failure_clears_the_tape);
    RUN_TEST(test_baud_zero_is_invalid);
    RUN_TEST(test_fuji_only_and_empty_records_are_invalid);
    RUN_TEST(test_empty_record_with_fsk_is_pulses);
    RUN_TEST(test_empty_chunks_keep_their_gap);
    RUN_TEST(test_long_fsk_silence_does_not_wrap);
    return UNITY_END();
}
