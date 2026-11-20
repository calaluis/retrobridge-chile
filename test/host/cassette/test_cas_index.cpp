// CasIndex (spec cassette-cas, C101): records and FSK runs of a .cas, the run join rule,
// broken and unsupported files (AC1, AC2, AC6, AC7). Format cases use minimal byte
// fragments; whole tapes are always the real ones from RB_CAS_SAMPLES, never copied here.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "rb/cassette/cas_index.hpp"
#include "unity.h"

using rb::cassette::CasIndex;
using rb::cassette::CasIndexResult;
using rb::cassette::CasSegment;
using rb::cassette::CasStatus;
using rb::cassette::MemoryTapeSource;
using rb::cassette::SegmentKind;
using rb::core::Span;

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
Bytes fsk(uint16_t gapMs, const Bytes& durations) { return chunk("fsk ", gapMs, durations); }
Bytes record(uint8_t fill) { return Bytes(132, fill); }
Bytes pulses() { return Bytes{0x10, 0x00, 0x20, 0x00}; }  // two LE16 durations

constexpr std::size_t kTable = 1024;
CasSegment g_table[kTable];

CasIndexResult index(const Bytes& file, std::size_t capacity = kTable)
{
    MemoryTapeSource source(Span<const uint8_t>(file.data(), file.size()));
    return CasIndex::build(source, Span<CasSegment>(g_table, capacity));
}

void assertStatus(CasStatus expected, const CasIndexResult& result)
{
    TEST_ASSERT_EQUAL(static_cast<int>(expected), static_cast<int>(result.status));
    if (expected != CasStatus::Ok) {
        TEST_ASSERT_EQUAL_UINT32(0, result.count);
    }
}

// A source that fails every read after the first `good` ones.
class FailingSource final : public rb::cassette::TapeSource {
public:
    FailingSource(const Bytes& bytes, int good) : inner_(Span<const uint8_t>(bytes.data(), bytes.size())), good_(good) {}
    uint32_t size() const override { return inner_.size(); }
    bool read(uint32_t offset, Span<uint8_t> dst) override { return good_-- > 0 && inner_.read(offset, dst); }

private:
    MemoryTapeSource inner_;
    int good_;
};

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

const char* samples() { return std::getenv("RB_CAS_SAMPLES"); }

}  // namespace

void setUp() {}
void tearDown() {}

// No baud chunk means 600 baud; each record keeps its gap, offset and length.
void test_records_default_to_600_baud()
{
    const Bytes file = fuji() + data(20000, record(0x55)) + data(250, record(0xAA));
    const CasIndexResult result = index(file);
    assertStatus(CasStatus::Ok, result);
    TEST_ASSERT_EQUAL_UINT32(2, result.count);
    TEST_ASSERT_EQUAL(static_cast<int>(SegmentKind::Data), static_cast<int>(g_table[0].kind));
    TEST_ASSERT_EQUAL_UINT16(20000, g_table[0].gapMs);
    TEST_ASSERT_EQUAL_UINT32(12 + 8, g_table[0].offset);
    TEST_ASSERT_EQUAL_UINT32(132, g_table[0].length);
    TEST_ASSERT_EQUAL_UINT32(600, g_table[0].baud);
    TEST_ASSERT_EQUAL_UINT16(250, g_table[1].gapMs);
    TEST_ASSERT_EQUAL_UINT32(12 + 8 + 132 + 8, g_table[1].offset);
}

void test_baud_chunks_set_the_rate_of_following_records()
{
    const Bytes file = fuji() + data(10, record(1)) + baud(800) + data(20, record(2)) + baud(750) + data(30, record(3));
    const CasIndexResult result = index(file);
    assertStatus(CasStatus::Ok, result);
    TEST_ASSERT_EQUAL_UINT32(3, result.count);
    TEST_ASSERT_EQUAL_UINT32(600, g_table[0].baud);
    TEST_ASSERT_EQUAL_UINT32(800, g_table[1].baud);
    TEST_ASSERT_EQUAL_UINT32(750, g_table[2].baud);
}

// AC1: an fsk chunk is a signal run, not silence.
void test_fsk_chunk_is_a_run()
{
    const Bytes file = fuji() + fsk(1500, pulses()) + data(0, record(7));
    const CasIndexResult result = index(file);
    assertStatus(CasStatus::Ok, result);
    TEST_ASSERT_EQUAL_UINT32(2, result.count);
    TEST_ASSERT_EQUAL(static_cast<int>(SegmentKind::Fsk), static_cast<int>(g_table[0].kind));
    TEST_ASSERT_EQUAL_UINT16(1500, g_table[0].gapMs);
    TEST_ASSERT_EQUAL_UINT32(12, g_table[0].offset);  // header of the chunk
    TEST_ASSERT_EQUAL_UINT32(4, g_table[0].length);
    TEST_ASSERT_EQUAL_UINT8(1, g_table[0].chunks);
}

// AC2: fsk chunks right after with gap 0 join the run; a gap opens a new run.
void test_zero_gap_fsk_chunks_join_one_run()
{
    const Bytes file = fuji() + fsk(100, pulses()) + fsk(0, pulses()) + fsk(0, pulses()) + fsk(5, pulses());
    const CasIndexResult result = index(file);
    assertStatus(CasStatus::Ok, result);
    TEST_ASSERT_EQUAL_UINT32(2, result.count);
    TEST_ASSERT_EQUAL_UINT8(3, g_table[0].chunks);
    TEST_ASSERT_EQUAL_UINT32(12, g_table[0].length);
    TEST_ASSERT_EQUAL_UINT16(100, g_table[0].gapMs);
    TEST_ASSERT_EQUAL_UINT8(1, g_table[1].chunks);
    TEST_ASSERT_EQUAL_UINT16(5, g_table[1].gapMs);
}

// AC2: any other chunk between two fsk chunks ends the run, even with gap 0.
void test_other_chunk_ends_a_run()
{
    const Bytes file = fuji() + fsk(100, pulses()) + baud(600) + fsk(0, pulses());
    const CasIndexResult result = index(file);
    assertStatus(CasStatus::Ok, result);
    TEST_ASSERT_EQUAL_UINT32(2, result.count);
    TEST_ASSERT_EQUAL_UINT8(1, g_table[0].chunks);
    TEST_ASSERT_EQUAL_UINT8(1, g_table[1].chunks);
    TEST_ASSERT_EQUAL_UINT16(0, g_table[1].gapMs);
}

// AC2: chunk 65 of a zero-gap chain starts a new run, with no gap.
void test_run_stops_at_64_chunks()
{
    Bytes file = fuji() + fsk(100, pulses());
    for (int i = 1; i < 65; ++i) {
        file = file + fsk(0, pulses());
    }
    const CasIndexResult result = index(file);
    assertStatus(CasStatus::Ok, result);
    TEST_ASSERT_EQUAL_UINT32(2, result.count);
    TEST_ASSERT_EQUAL_UINT8(64, g_table[0].chunks);
    TEST_ASSERT_EQUAL_UINT8(1, g_table[1].chunks);
    TEST_ASSERT_EQUAL_UINT16(0, g_table[1].gapMs);
}

// A tape with fsk only (turbo software type B) is playable now.
void test_fsk_only_tape_is_ok()
{
    const CasIndexResult result = index(fuji() + fsk(0, pulses()));
    assertStatus(CasStatus::Ok, result);
    TEST_ASSERT_EQUAL_UINT32(1, result.count);
}

void test_unknown_chunks_are_skipped()
{
    const Bytes file = fuji() + chunk("xyz?", 9, {1, 2, 3}) + data(5, record(1)) + fuji();
    const CasIndexResult result = index(file);
    assertStatus(CasStatus::Ok, result);
    TEST_ASSERT_EQUAL_UINT32(1, result.count);
}

// An empty record keeps its gap as a record of length 0.
void test_empty_record_keeps_its_gap()
{
    const Bytes file = fuji() + data(300, {}) + data(5, record(1));
    const CasIndexResult result = index(file);
    assertStatus(CasStatus::Ok, result);
    TEST_ASSERT_EQUAL_UINT32(2, result.count);
    TEST_ASSERT_EQUAL_UINT32(0, g_table[0].length);
    TEST_ASSERT_EQUAL_UINT16(300, g_table[0].gapMs);
}

// AC6: broken files.
// A last chunk with no payload ends exactly at the end of the file: still a good tape.
void test_header_ending_at_eof_is_ok()
{
    const CasIndexResult result = index(fuji() + data(0, record(1)) + baud(600));
    assertStatus(CasStatus::Ok, result);
    TEST_ASSERT_EQUAL_UINT32(1, result.count);
}

// Only the exact `FUJI` type opens a tape.
void test_other_first_type_is_invalid()
{
    assertStatus(CasStatus::Invalid, index(chunk("FUJX", 0, {}) + data(0, record(1))));
    assertStatus(CasStatus::Invalid, index(chunk("Fuji", 0, {}) + data(0, record(1))));
}

void test_missing_fuji_is_invalid() { assertStatus(CasStatus::Invalid, index(data(0, record(1)))); }
void test_cut_header_is_invalid() { assertStatus(CasStatus::Invalid, index(fuji() + data(0, record(1)) + Bytes{'d', 'a'})); }

void test_cut_chunk_is_invalid()
{
    Bytes file = fuji() + data(0, record(1));
    file.pop_back();
    assertStatus(CasStatus::Invalid, index(file));
}

void test_odd_fsk_length_is_invalid() { assertStatus(CasStatus::Invalid, index(fuji() + fsk(0, {1, 2, 3}))); }
void test_baud_zero_is_invalid() { assertStatus(CasStatus::Invalid, index(fuji() + baud(0) + data(0, record(1)))); }
void test_nothing_to_play_is_invalid() { assertStatus(CasStatus::Invalid, index(fuji() + data(5, {}) + fsk(5, {}))); }
void test_empty_file_is_invalid() { assertStatus(CasStatus::Invalid, index(Bytes{})); }

void test_pwm_chunk_is_unsupported()
{
    assertStatus(CasStatus::Unsupported, index(fuji() + data(0, record(1)) + chunk("pwms", 0, {1, 0})));
}

void test_more_segments_than_the_table_is_full()
{
    const Bytes file = fuji() + data(1, record(1)) + data(2, record(2)) + data(3, record(3));
    assertStatus(CasStatus::Full, index(file, 2));
}

void test_read_error_is_reported()
{
    const Bytes file = fuji() + data(1, record(1)) + data(2, record(2));
    FailingSource source(file, 1);
    const CasIndexResult result = CasIndex::build(source, Span<CasSegment>(g_table, kTable));
    assertStatus(CasStatus::ReadError, result);
}

// AC1: the real International Karate: 106 records at 600 and 750 baud, 107 fsk chunks as runs.
void test_real_international_karate()
{
    if (samples() == nullptr) {
        TEST_IGNORE_MESSAGE("RB_CAS_SAMPLES not set");
    }
    Bytes file;
    if (!readFile(std::string(samples()) + "/international_karate.cas", file)) {
        TEST_IGNORE_MESSAGE("international_karate.cas not found");
    }
    const CasIndexResult result = index(file);
    assertStatus(CasStatus::Ok, result);
    uint32_t records = 0;
    uint32_t fskChunks = 0;
    bool saw600 = false;
    bool saw750 = false;
    for (uint32_t i = 0; i < result.count; ++i) {
        if (g_table[i].kind == SegmentKind::Data) {
            ++records;
            saw600 = saw600 || g_table[i].baud == 600;
            saw750 = saw750 || g_table[i].baud == 750;
        } else {
            fskChunks += g_table[i].chunks;
            TEST_ASSERT_TRUE(g_table[i].length > 0 || g_table[i].chunks > 0);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(106, records);
    TEST_ASSERT_EQUAL_UINT32(107, fskChunks);
    TEST_ASSERT_TRUE(saw600);
    TEST_ASSERT_TRUE(saw750);
}

// AC7: every .cas under RB_CAS_SAMPLES (subfolders too) reads Ok.
void test_every_real_tape_reads()
{
    if (samples() == nullptr) {
        TEST_IGNORE_MESSAGE("RB_CAS_SAMPLES not set");
    }
    std::error_code error;
    std::filesystem::recursive_directory_iterator it(samples(), error);
    if (error) {
        TEST_IGNORE_MESSAGE("RB_CAS_SAMPLES not readable");
    }
    uint32_t tapes = 0;
    for (; !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        const std::filesystem::path& path = it->path();
        std::string extension = path.extension().string();
        for (char& c : extension) {
            c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        }
        if (extension != ".cas") {
            continue;
        }
        const std::string name = path.string();
        Bytes file;
        TEST_ASSERT_TRUE_MESSAGE(readFile(name, file), name.c_str());
        const CasIndexResult result = index(file);
        if (path.parent_path().filename() == "fsktest") {
            // The owner's FujiNet memory fixtures: one fsk chunk of odd length (513, 65535),
            // which casFSK's fsk_scan_run also refuses. Not game tapes.
            TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(CasStatus::Invalid), static_cast<int>(result.status),
                                      name.c_str());
            continue;
        }
        TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(CasStatus::Ok), static_cast<int>(result.status), name.c_str());
        TEST_ASSERT_TRUE_MESSAGE(result.count > 0, name.c_str());
        ++tapes;
    }
    TEST_ASSERT_FALSE_MESSAGE(error, "walking RB_CAS_SAMPLES failed");
    TEST_ASSERT_TRUE(tapes > 0);
    std::printf("real tapes read: %u\n", static_cast<unsigned>(tapes));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_records_default_to_600_baud);
    RUN_TEST(test_baud_chunks_set_the_rate_of_following_records);
    RUN_TEST(test_fsk_chunk_is_a_run);
    RUN_TEST(test_zero_gap_fsk_chunks_join_one_run);
    RUN_TEST(test_other_chunk_ends_a_run);
    RUN_TEST(test_run_stops_at_64_chunks);
    RUN_TEST(test_fsk_only_tape_is_ok);
    RUN_TEST(test_unknown_chunks_are_skipped);
    RUN_TEST(test_empty_record_keeps_its_gap);
    RUN_TEST(test_missing_fuji_is_invalid);
    RUN_TEST(test_cut_header_is_invalid);
    RUN_TEST(test_cut_chunk_is_invalid);
    RUN_TEST(test_odd_fsk_length_is_invalid);
    RUN_TEST(test_baud_zero_is_invalid);
    RUN_TEST(test_header_ending_at_eof_is_ok);
    RUN_TEST(test_other_first_type_is_invalid);
    RUN_TEST(test_nothing_to_play_is_invalid);
    RUN_TEST(test_empty_file_is_invalid);
    RUN_TEST(test_pwm_chunk_is_unsupported);
    RUN_TEST(test_more_segments_than_the_table_is_full);
    RUN_TEST(test_read_error_is_reported);
    RUN_TEST(test_real_international_karate);
    RUN_TEST(test_every_real_tape_reads);
    return UNITY_END();
}
