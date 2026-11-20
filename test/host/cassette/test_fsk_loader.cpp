// FskLoader (spec cassette-cas, C103, AC4): a run is read in blocks of at most 512 bytes into
// a bank the caller owns; the start gate waits for 20 s of signal and the first fill; reads
// are retried; nothing is allocated while loading or playing. The whole-run check uses the
// real turbo_software_zorro.cas (one run of about 15 minutes) from RB_CAS_SAMPLES.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "rb/cassette/cas_index.hpp"
#include "rb/cassette/fsk_loader.hpp"
#include "rb/cassette/fsk_wave.hpp"
#include "unity.h"

using rb::cassette::CasIndex;
using rb::cassette::CasIndexResult;
using rb::cassette::CasSegment;
using rb::cassette::CasStatus;
using rb::cassette::FskLoader;
using rb::cassette::FskWave;
using rb::cassette::LoaderState;
using rb::cassette::LoaderStep;
using rb::cassette::MemoryTapeSource;
using rb::cassette::SegmentKind;
using rb::cassette::StartGate;
using rb::cassette::TapeSource;
using rb::cassette::ValueFetch;
using rb::core::Span;

// ---- allocation counter (same mechanism as test_netsio_transport_allocations) ----
namespace {
unsigned long g_allocations = 0;
void* counted(std::size_t size) noexcept
{
    ++g_allocations;
    return std::malloc(size == 0 ? 1 : size);
}
}  // namespace

void* operator new(std::size_t size)
{
    void* p = counted(size);
    if (p == nullptr) {
        std::abort();
    }
    return p;
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return counted(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return counted(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

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

// `count` values of `units` each (1 unit = 0.1 ms).
Bytes values(std::size_t count, uint16_t units)
{
    Bytes out;
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(static_cast<uint8_t>(units & 0xFF));
        out.push_back(static_cast<uint8_t>(units >> 8));
    }
    return out;
}

Bytes fuji() { return chunk("FUJI", 0, {}); }

// Logs every read; can fail the reads listed in `failAt` (by read number, from 0).
class LoggingSource final : public TapeSource {
public:
    explicit LoggingSource(const Bytes& bytes) : inner_(Span<const uint8_t>(bytes.data(), bytes.size())) {}
    uint32_t size() const override { return inner_.size(); }
    bool read(uint32_t offset, Span<uint8_t> dst) override
    {
        const uint32_t n = count++;
        sizes.push_back(static_cast<uint32_t>(dst.size()));
        for (uint32_t f : failAt) {
            if (f == n) {
                return false;
            }
        }
        return inner_.read(offset, dst);
    }
    std::vector<uint32_t> sizes;
    std::vector<uint32_t> failAt;
    uint32_t count = 0;

private:
    MemoryTapeSource inner_;
};

CasSegment g_table[1024];
uint8_t g_bank[1u << 20];  // 1 MiB: larger than any real run

CasSegment firstRun(TapeSource& source)
{
    const CasIndexResult index = CasIndex::build(source, Span<CasSegment>(g_table, 1024));
    TEST_ASSERT_EQUAL(static_cast<int>(CasStatus::Ok), static_cast<int>(index.status));
    for (uint32_t i = 0; i < index.count; ++i) {
        if (g_table[i].kind == SegmentKind::Fsk) {
            return g_table[i];
        }
    }
    TEST_FAIL_MESSAGE("no fsk run");
    return CasSegment{};
}

LoaderStep runToEnd(FskLoader& loader, uint32_t limit = 100000)
{
    LoaderStep step = LoaderStep::Progress;
    while (step == LoaderStep::Progress && limit-- > 0) {
        step = loader.step();
    }
    return step;
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

}  // namespace

void setUp() {}
void tearDown() {}

// One 2000-byte chunk: a header read, then 512, 512, 512, 464; all values published, final.
void test_reads_one_block_at_a_time()
{
    const Bytes file = fuji() + chunk("fsk ", 50, values(1000, 7));
    LoggingSource source(file);
    const CasSegment run = firstRun(source);
    source.sizes.clear();
    FskLoader loader;
    loader.bind(Span<uint8_t>(g_bank, sizeof g_bank));
    TEST_ASSERT_TRUE(loader.start(source, run));
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderStep::Done), static_cast<int>(runToEnd(loader)));
    const std::vector<uint32_t> expected = {8, 512, 512, 512, 464};
    TEST_ASSERT_EQUAL_size_t(expected.size(), source.sizes.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT32(expected[i], source.sizes[i]);
    }
    TEST_ASSERT_EQUAL_UINT32(1000, loader.published());
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderState::Final), static_cast<int>(loader.state()));
}

// A run of three chunks: headers are skipped and payloads packed back to back; a block
// never crosses into the next chunk's header.
void test_joined_chunks_are_packed()
{
    Bytes a = values(300, 1);
    Bytes b = values(5, 2);
    Bytes c = values(1, 3);
    const Bytes file = fuji() + chunk("fsk ", 10, a) + chunk("fsk ", 0, b) + chunk("fsk ", 0, c) +
                       chunk("data", 0, {1, 2});
    LoggingSource source(file);
    const CasSegment run = firstRun(source);
    TEST_ASSERT_EQUAL_UINT8(3, run.chunks);
    FskLoader loader;
    loader.bind(Span<uint8_t>(g_bank, sizeof g_bank));
    TEST_ASSERT_TRUE(loader.start(source, run));
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderStep::Done), static_cast<int>(runToEnd(loader)));
    TEST_ASSERT_EQUAL_UINT32(306, loader.published());
    uint16_t v = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(ValueFetch::Value), static_cast<int>(loader.next(0, v)));
    TEST_ASSERT_EQUAL_UINT16(1, v);
    TEST_ASSERT_EQUAL(static_cast<int>(ValueFetch::Value), static_cast<int>(loader.next(300, v)));
    TEST_ASSERT_EQUAL_UINT16(2, v);
    TEST_ASSERT_EQUAL(static_cast<int>(ValueFetch::Value), static_cast<int>(loader.next(305, v)));
    TEST_ASSERT_EQUAL_UINT16(3, v);
    TEST_ASSERT_EQUAL(static_cast<int>(ValueFetch::End), static_cast<int>(loader.next(306, v)));
}

// Before a value is published the consumer gets Underrun, never stale bytes.
void test_unpublished_value_is_underrun()
{
    const Bytes file = fuji() + chunk("fsk ", 0, values(600, 9));
    LoggingSource source(file);
    const CasSegment run = firstRun(source);
    FskLoader loader;
    loader.bind(Span<uint8_t>(g_bank, sizeof g_bank));
    TEST_ASSERT_TRUE(loader.start(source, run));
    uint16_t v = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(ValueFetch::Underrun), static_cast<int>(loader.next(0, v)));
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderStep::Progress), static_cast<int>(loader.step()));  // header
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderStep::Progress), static_cast<int>(loader.step()));  // 512 bytes
    TEST_ASSERT_EQUAL_UINT32(256, loader.published());
    TEST_ASSERT_EQUAL(static_cast<int>(ValueFetch::Value), static_cast<int>(loader.next(255, v)));
    TEST_ASSERT_EQUAL(static_cast<int>(ValueFetch::Underrun), static_cast<int>(loader.next(256, v)));
    TEST_ASSERT_EQUAL_size_t(256 * 2, loader.piece(0).size());
    TEST_ASSERT_EQUAL_size_t(6, loader.piece(253).size());
    TEST_ASSERT_EQUAL_size_t(0, loader.piece(256).size());
}

// The gate opens only with 20 s of signal and 1536 values: 2000 values of 0.1 s each =
// 200 s; the gate opens at the first step that publishes the 1536th value.
void test_gate_waits_for_runway_and_prefill()
{
    const Bytes file = fuji() + chunk("fsk ", 0, values(2000, 1000));
    LoggingSource source(file);
    const CasSegment run = firstRun(source);
    FskLoader loader;
    loader.bind(Span<uint8_t>(g_bank, sizeof g_bank));
    TEST_ASSERT_TRUE(loader.start(source, run));
    while (loader.startGate() == StartGate::Waiting) {
        TEST_ASSERT_TRUE(loader.published() < FskLoader::kPrefillValues);
        TEST_ASSERT_EQUAL(static_cast<int>(LoaderStep::Progress), static_cast<int>(loader.step()));
    }
    TEST_ASSERT_EQUAL(static_cast<int>(StartGate::Ready), static_cast<int>(loader.startGate()));
    TEST_ASSERT_TRUE(loader.published() >= FskLoader::kPrefillValues);
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderState::Loading), static_cast<int>(loader.state()));
}

// Long values, few of them: the prefill is met by the run being final, the runway too.
void test_gate_opens_when_a_short_run_is_final()
{
    const Bytes file = fuji() + chunk("fsk ", 0, values(10, 5));  // 5 ms of signal
    LoggingSource source(file);
    const CasSegment run = firstRun(source);
    FskLoader loader;
    loader.bind(Span<uint8_t>(g_bank, sizeof g_bank));
    TEST_ASSERT_TRUE(loader.start(source, run));
    TEST_ASSERT_EQUAL(static_cast<int>(StartGate::Waiting), static_cast<int>(loader.startGate()));
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderStep::Done), static_cast<int>(runToEnd(loader)));
    TEST_ASSERT_EQUAL(static_cast<int>(StartGate::Ready), static_cast<int>(loader.startGate()));
}

// Enough values but not enough time: 1600 values of 1 ms = 1.6 s < 20 s; waits until final.
void test_gate_needs_the_runway_too()
{
    const Bytes file = fuji() + chunk("fsk ", 0, values(1600, 10)) + chunk("fsk ", 0, values(10, 10));
    LoggingSource source(file);
    const CasSegment run = firstRun(source);
    FskLoader loader;
    loader.bind(Span<uint8_t>(g_bank, sizeof g_bank));
    TEST_ASSERT_TRUE(loader.start(source, run));
    while (loader.published() < 1600) {
        TEST_ASSERT_EQUAL(static_cast<int>(LoaderStep::Progress), static_cast<int>(loader.step()));
    }
    TEST_ASSERT_EQUAL(static_cast<int>(StartGate::Waiting), static_cast<int>(loader.startGate()));
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderStep::Done), static_cast<int>(runToEnd(loader)));
    TEST_ASSERT_EQUAL(static_cast<int>(StartGate::Ready), static_cast<int>(loader.startGate()));
}

// A failed read is retried from the same place; a good read restores the budget.
void test_failed_reads_are_retried()
{
    const Bytes file = fuji() + chunk("fsk ", 0, values(600, 9));
    LoggingSource source(file);
    const CasSegment run = firstRun(source);
    source.count = 0;
    source.sizes.clear();
    source.failAt = {1, 2, 4, 5};  // two failures on block 1, two on block 2
    FskLoader loader;
    loader.bind(Span<uint8_t>(g_bank, sizeof g_bank));
    TEST_ASSERT_TRUE(loader.start(source, run));
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderStep::Done), static_cast<int>(runToEnd(loader)));
    TEST_ASSERT_EQUAL_UINT32(600, loader.published());
    TEST_ASSERT_EQUAL_UINT32(4, loader.faults());
    TEST_ASSERT_EQUAL_UINT32(512, source.sizes[1]);
    TEST_ASSERT_EQUAL_UINT32(512, source.sizes[3]);  // the retry asks for the same block
}

// Three failures in a row are retried; the fourth fails the run (as casFSKLoader's
// read_fault), and what was published stays.
void test_three_retries_then_the_run_fails()
{
    const Bytes file = fuji() + chunk("fsk ", 0, values(600, 9));
    LoggingSource three(file);
    const CasSegment run = firstRun(three);
    three.count = 0;
    three.failAt = {2, 3, 4};
    FskLoader retried;
    retried.bind(Span<uint8_t>(g_bank, sizeof g_bank));
    TEST_ASSERT_TRUE(retried.start(three, run));
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderStep::Done), static_cast<int>(runToEnd(retried)));

    LoggingSource source(file);
    source.failAt = {2, 3, 4, 5};
    FskLoader loader;
    loader.bind(Span<uint8_t>(g_bank, sizeof g_bank));
    TEST_ASSERT_TRUE(loader.start(source, run));
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderStep::Failed), static_cast<int>(runToEnd(loader)));
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderState::Failed), static_cast<int>(loader.state()));
    TEST_ASSERT_EQUAL_UINT32(256, loader.published());
    TEST_ASSERT_EQUAL(static_cast<int>(StartGate::Failed), static_cast<int>(loader.startGate()));
    uint16_t v = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(ValueFetch::Value), static_cast<int>(loader.next(0, v)));
    TEST_ASSERT_EQUAL(static_cast<int>(ValueFetch::Underrun), static_cast<int>(loader.next(256, v)));
}

// A header that no longer says `fsk ` (the file changed under the index) fails the run.
void test_changed_file_fails_the_run()
{
    Bytes file = fuji() + chunk("fsk ", 0, values(4, 9)) + chunk("fsk ", 0, values(4, 9));
    MemoryTapeSource indexSource(Span<const uint8_t>(file.data(), file.size()));
    const CasSegment run = firstRun(indexSource);
    Bytes changed = file;
    changed[8 + 8 + 8] = 'd';  // second chunk header: "dsk "
    MemoryTapeSource source(Span<const uint8_t>(changed.data(), changed.size()));
    FskLoader loader;
    loader.bind(Span<uint8_t>(g_bank, sizeof g_bank));
    TEST_ASSERT_TRUE(loader.start(source, run));
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderStep::Failed), static_cast<int>(runToEnd(loader)));
    TEST_ASSERT_EQUAL_UINT32(4, loader.published());
}

void test_stop_request_stops_the_producer()
{
    const Bytes file = fuji() + chunk("fsk ", 0, values(2000, 1000));
    LoggingSource source(file);
    const CasSegment run = firstRun(source);
    FskLoader loader;
    loader.bind(Span<uint8_t>(g_bank, sizeof g_bank));
    TEST_ASSERT_TRUE(loader.start(source, run));
    (void)loader.step();
    loader.requestStop();
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderStep::Stopped), static_cast<int>(loader.step()));
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderState::Stopped), static_cast<int>(loader.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(StartGate::Failed), static_cast<int>(loader.startGate()));
}

// A stop asked after the run is final changes nothing: the whole run stays playable.
void test_stop_after_final_changes_nothing()
{
    const Bytes file = fuji() + chunk("fsk ", 0, values(10, 9));
    LoggingSource source(file);
    const CasSegment run = firstRun(source);
    FskLoader loader;
    loader.bind(Span<uint8_t>(g_bank, sizeof g_bank));
    TEST_ASSERT_TRUE(loader.start(source, run));
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderStep::Done), static_cast<int>(runToEnd(loader)));
    loader.requestStop();
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderStep::Done), static_cast<int>(loader.step()));
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderState::Final), static_cast<int>(loader.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(StartGate::Ready), static_cast<int>(loader.startGate()));
}

// A run larger than the bank, or a data segment, is refused before any read.
void test_start_refuses_what_does_not_fit()
{
    const Bytes file = fuji() + chunk("fsk ", 0, values(600, 9)) + chunk("data", 0, {1, 2});
    LoggingSource source(file);
    const CasSegment run = firstRun(source);
    FskLoader loader;
    loader.bind(Span<uint8_t>(g_bank, 1024));
    TEST_ASSERT_FALSE(loader.start(source, run));
    loader.bind(Span<uint8_t>(g_bank, sizeof g_bank));
    CasSegment data = run;
    data.kind = SegmentKind::Data;
    TEST_ASSERT_FALSE(loader.start(source, data));
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderState::Idle), static_cast<int>(loader.state()));
}

// ADR-011: loading and reading values allocate nothing.
void test_no_allocation_while_loading_and_playing()
{
    const Bytes file = fuji() + chunk("fsk ", 0, values(3000, 9));
    MemoryTapeSource source(Span<const uint8_t>(file.data(), file.size()));
    const CasSegment run = firstRun(source);
    FskLoader loader;
    loader.bind(Span<uint8_t>(g_bank, sizeof g_bank));
    TEST_ASSERT_TRUE(loader.start(source, run));
    const unsigned long before = g_allocations;
    uint16_t v = 0;
    uint32_t index = 0;
    while (loader.step() == LoaderStep::Progress) {
        while (loader.next(index, v) == ValueFetch::Value) {
            ++index;
        }
        (void)loader.startGate();
        (void)loader.piece(0);
    }
    TEST_ASSERT_EQUAL_UINT32(3000, index);
    TEST_ASSERT_EQUAL_UINT32(before, g_allocations);
    // Positive control: the counter is live.
    int* probe = new int(1);
    TEST_ASSERT_EQUAL_UINT32(before + 1, g_allocations);
    delete probe;
}

// AC4 on the real tape: Zorro's single run (about 15 minutes) loads whole, block by block;
// the gate opens long before the end; the published bytes equal the file's payload.
void test_real_turbo_software_zorro()
{
    const char* samples = std::getenv("RB_CAS_SAMPLES");
    if (samples == nullptr) {
        TEST_IGNORE_MESSAGE("RB_CAS_SAMPLES not set");
    }
    Bytes file;
    if (!readFile(std::string(samples) + "/turbo_software_zorro.cas", file)) {
        TEST_IGNORE_MESSAGE("turbo_software_zorro.cas not found");
    }
    LoggingSource source(file);
    const CasSegment run = firstRun(source);
    source.sizes.clear();
    FskLoader loader;
    loader.bind(Span<uint8_t>(g_bank, sizeof g_bank));
    TEST_ASSERT_TRUE(loader.start(source, run));
    uint32_t readsAtGate = 0;
    LoaderStep step = LoaderStep::Progress;
    while (step == LoaderStep::Progress) {
        step = loader.step();
        if (readsAtGate == 0 && loader.startGate() == StartGate::Ready) {
            readsAtGate = loader.reads();
        }
    }
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderStep::Done), static_cast<int>(step));
    TEST_ASSERT_EQUAL_UINT32(run.length / 2, loader.published());
    for (uint32_t size : source.sizes) {
        TEST_ASSERT_TRUE(size <= FskLoader::kBlockBytes);
    }
    // The bank holds the payloads exactly, chunk after chunk.
    uint32_t at = run.offset;
    uint32_t value = 0;
    uint64_t ticks = 0;
    for (uint8_t c = 0; c < run.chunks; ++c) {
        const uint16_t length = static_cast<uint16_t>(file[at + 4] | (file[at + 5] << 8));
        for (uint32_t i = 0; i < length; i += 2, ++value) {
            uint16_t v = 0;
            TEST_ASSERT_EQUAL(static_cast<int>(ValueFetch::Value), static_cast<int>(loader.next(value, v)));
            TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(file[at + 8 + i] | (file[at + 9 + i] << 8)), v);
        }
        ticks += FskWave::sumTicks(Span<const uint8_t>(file.data() + at + 8, length));
        at += 8u + length;
    }
    TEST_ASSERT_TRUE(readsAtGate > 0 && readsAtGate < loader.reads());
    std::printf("zorro: %u values, %u reads, gate open after %u reads, signal %llu s\n",
                static_cast<unsigned>(loader.published()), static_cast<unsigned>(loader.reads()),
                static_cast<unsigned>(readsAtGate), static_cast<unsigned long long>(ticks / 1000000ULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reads_one_block_at_a_time);
    RUN_TEST(test_joined_chunks_are_packed);
    RUN_TEST(test_unpublished_value_is_underrun);
    RUN_TEST(test_gate_waits_for_runway_and_prefill);
    RUN_TEST(test_gate_opens_when_a_short_run_is_final);
    RUN_TEST(test_gate_needs_the_runway_too);
    RUN_TEST(test_failed_reads_are_retried);
    RUN_TEST(test_three_retries_then_the_run_fails);
    RUN_TEST(test_stop_after_final_changes_nothing);
    RUN_TEST(test_changed_file_fails_the_run);
    RUN_TEST(test_stop_request_stops_the_producer);
    RUN_TEST(test_start_refuses_what_does_not_fit);
    RUN_TEST(test_no_allocation_while_loading_and_playing);
    RUN_TEST(test_real_turbo_software_zorro);
    return UNITY_END();
}
