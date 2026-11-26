// FujiTapeLoop (cassette-motor-fujinet): the RetroBridge adapter that plays one record per step the way the
// beta v1.6.2-atari-fsk-beta.1 does (docs/diseno/cassette-motor-fujinet.md §2). Synthetic tapes for each rule;
// the real International Karate and PAL Alien Ambush (RB_CAS_SAMPLES) checked against CasIndex, RetroBridge's
// own reading of the same file, record by record.
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "casFSK.h"
#include "rb/cassette/cas_index.hpp"
#include "rb/cassette/tape_source.hpp"
#include "rb/fujinet/fuji_tape_loop.hpp"
#include "unity.h"

using rb::cassette::MemoryTapeSource;
using rb::fujinet::FujiStep;
using rb::fujinet::FujiStepKind;
using rb::fujinet::FujiTapeLoop;
using rb::fujinet::FskRunOutcome;
using Bytes = std::vector<uint8_t>;

void setUp() {}
void tearDown() {}

namespace {

enum class Ev : uint8_t { Baud, Write, Flush, Wait, Fsk };

struct Event {
    Ev kind;
    uint32_t value;  // Baud: rate; Write: byte count; Wait: ms; Fsk: header offset
    Bytes bytes;     // Write only
};

class Bus final : public rb::fujinet::FujiTapeBus {
public:
    std::vector<Event> events;
    uint32_t dropAfterMs = UINT32_MAX;  // MOTOR reads as dropped once this many ms have been waited
    uint32_t waited = 0;

    unsigned refuseWriteAt = UINT32_MAX;  // the n-th write (from 1) answers false: stop the step
    unsigned refuseFlushAt = UINT32_MAX;  // the n-th flush (from 1) answers false
    bool refuseBaud = false;
    unsigned writes = 0;
    unsigned flushes = 0;

    bool setBaud(uint32_t baud) override
    {
        events.push_back({Ev::Baud, baud, {}});
        return !refuseBaud;
    }
    bool write(const uint8_t* bytes, uint32_t count) override
    {
        events.push_back({Ev::Write, count, Bytes(bytes, bytes + count)});
        return ++writes != refuseWriteAt;
    }
    bool flushOutput() override
    {
        events.push_back({Ev::Flush, 0, {}});
        return ++flushes != refuseFlushAt;
    }
    void waitOneMs() override
    {
        ++waited;
        if (!events.empty() && events.back().kind == Ev::Wait) {
            ++events.back().value;
        } else {
            events.push_back({Ev::Wait, 1, {}});
        }
    }
    bool motorDropped() const override { return waited >= dropAfterMs; }
    uint32_t stopAfterMs = UINT32_MAX;  // stopWanted() once this many ms have been waited
    bool stopWanted() override { return waited >= stopAfterMs; }
};

// Plays nothing; answers with the owner's fsk_scan_run (casFSK.h): the next offset the beta would use.
class Player final : public rb::fujinet::FskRunPlayer {
public:
    Player(const Bytes& file, Bus& bus) : file_(file), bus_(bus) {}
    bool fail = false;

    FskRunOutcome play(uint32_t headerOffset, uint16_t /*chunkLength*/) override
    {
        bus_.events.push_back({Ev::Fsk, headerOffset, {}});
        FskRunOutcome out;
        FSKRunInfo run{};
        if (fail || !fsk_scan_run(&Player::read, this, file_.size(), headerOffset, run)) {
            return out;
        }
        out.ok = true;
        out.nextOffset = static_cast<uint32_t>(run.next_offset);
        return out;
    }

private:
    static size_t read(void* ctx, size_t offset, uint8_t* dst, size_t n)
    {
        const Bytes& f = static_cast<Player*>(ctx)->file_;
        if (offset >= f.size()) {
            return 0;
        }
        const size_t got = std::min(n, f.size() - offset);
        std::memcpy(dst, f.data() + offset, got);
        return got;
    }
    const Bytes& file_;
    Bus& bus_;
};

void chunk(Bytes& f, const char* type, uint16_t aux, const Bytes& payload)
{
    f.insert(f.end(), type, type + 4);
    f.push_back(static_cast<uint8_t>(payload.size() & 0xff));
    f.push_back(static_cast<uint8_t>(payload.size() >> 8));
    f.push_back(static_cast<uint8_t>(aux & 0xff));
    f.push_back(static_cast<uint8_t>(aux >> 8));
    f.insert(f.end(), payload.begin(), payload.end());
}

Bytes fill(size_t n, uint8_t first)
{
    Bytes b(n);
    for (size_t i = 0; i < n; ++i) {
        b[i] = static_cast<uint8_t>(first + i);
    }
    return b;
}

bool readFile(const std::string& path, Bytes& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

}  // namespace

// enable(): back to 600 baud (the beta's sio_enable_cassette); the offset stays where it was.
void test_enable_sets_600_baud()
{
    Bytes f;
    chunk(f, "FUJI", 0, {});
    MemoryTapeSource src({f.data(), f.size()});
    Bus bus;
    Player player(f, bus);
    FujiTapeLoop loop(src, bus, player);
    loop.enable();
    TEST_ASSERT_EQUAL(1, bus.events.size());
    TEST_ASSERT_EQUAL(Ev::Baud, bus.events[0].kind);
    TEST_ASSERT_EQUAL_UINT32(600, bus.events[0].value);
}

// A data record: the baud chunk before it sets the rate, then its whole gap, then its bytes in pieces of
// 256, each one flushed before the next. The offset moves past it; the record time is casTape.h's.
void test_data_record_goes_out_after_its_gap_in_pieces_of_256()
{
    Bytes f;
    chunk(f, "FUJI", 0, {});
    chunk(f, "baud", 750, {});
    const Bytes payload = fill(600, 7);
    chunk(f, "data", 25, payload);
    MemoryTapeSource src({f.data(), f.size()});
    Bus bus;
    Player player(f, bus);
    FujiTapeLoop loop(src, bus, player);

    const FujiStep step = loop.step();

    TEST_ASSERT_EQUAL(FujiStepKind::Data, step.kind);
    TEST_ASSERT_EQUAL_UINT32(f.size(), step.offset);
    TEST_ASSERT_EQUAL_UINT32(25 + (600 * 10 * 1000 + 749) / 750, step.recordMs);
    TEST_ASSERT_EQUAL_UINT32(750, loop.baud());
    const Ev order[] = {Ev::Baud, Ev::Wait, Ev::Write, Ev::Flush, Ev::Write, Ev::Flush, Ev::Write, Ev::Flush};
    TEST_ASSERT_EQUAL(sizeof order / sizeof order[0], bus.events.size());
    for (size_t i = 0; i < bus.events.size(); ++i) {
        TEST_ASSERT_EQUAL(order[i], bus.events[i].kind);
    }
    TEST_ASSERT_EQUAL_UINT32(750, bus.events[0].value);
    TEST_ASSERT_EQUAL_UINT32(25, bus.events[1].value);
    TEST_ASSERT_EQUAL_UINT32(256, bus.events[2].value);
    TEST_ASSERT_EQUAL_UINT32(256, bus.events[4].value);
    TEST_ASSERT_EQUAL_UINT32(88, bus.events[6].value);
    Bytes sent;
    for (const Event& e : bus.events) {
        sent.insert(sent.end(), e.bytes.begin(), e.bytes.end());
    }
    TEST_ASSERT_TRUE(sent == payload);
}

// MOTOR drops with more than 1000 ms of the gap left: nothing goes out and the step goes back to where it
// started (here the start of the file: the FUJI and baud chunks are read again by the next step).
void test_motor_drop_early_in_the_gap_replays_from_the_step_start()
{
    Bytes f;
    chunk(f, "FUJI", 0, {});
    chunk(f, "baud", 600, {});
    chunk(f, "data", 3000, fill(4, 0));
    MemoryTapeSource src({f.data(), f.size()});
    Bus bus;
    Player player(f, bus);
    FujiTapeLoop loop(src, bus, player);
    bus.dropAfterMs = 1500;  // 1500 ms waited: 1500 left, more than 1000

    const FujiStep step = loop.step();

    TEST_ASSERT_EQUAL(FujiStepKind::Replay, step.kind);
    TEST_ASSERT_EQUAL_UINT32(0, step.offset);
    TEST_ASSERT_EQUAL_UINT32(0, loop.offset());
    TEST_ASSERT_EQUAL_UINT32(1500, bus.waited);
    for (const Event& e : bus.events) {
        TEST_ASSERT_TRUE(e.kind != Ev::Write);
    }
}

// The last 1000 ms of a gap do not look at MOTOR: a drop there lets the record go out.
void test_motor_drop_in_the_last_second_of_the_gap_is_ignored()
{
    Bytes f;
    chunk(f, "FUJI", 0, {});
    chunk(f, "data", 3000, fill(4, 0));
    MemoryTapeSource src({f.data(), f.size()});
    Bus bus;
    Player player(f, bus);
    FujiTapeLoop loop(src, bus, player);
    bus.dropAfterMs = 2000;  // 2000 waited: 1000 left, not more than 1000

    const FujiStep step = loop.step();

    TEST_ASSERT_EQUAL(FujiStepKind::Data, step.kind);
    TEST_ASSERT_EQUAL_UINT32(3000, bus.waited);
}

// Boundary: 1001 ms left is more than 1000, so the drop counts.
void test_motor_drop_with_1001_ms_left_counts()
{
    Bytes f;
    chunk(f, "FUJI", 0, {});
    chunk(f, "data", 3000, fill(4, 0));
    MemoryTapeSource src({f.data(), f.size()});
    Bus bus;
    Player player(f, bus);
    FujiTapeLoop loop(src, bus, player);
    bus.dropAfterMs = 1999;

    const FujiStep step = loop.step();

    TEST_ASSERT_EQUAL(FujiStepKind::Replay, step.kind);
    TEST_ASSERT_EQUAL_UINT32(1999, bus.waited);
}

// An fsk chunk: its gap, then the owner's player gets its header offset; ok moves to the player's next
// offset (two fsk chunks with gap 0 are one run). A run that did not play goes back to the step start.
void test_fsk_run_goes_to_the_player_after_its_gap()
{
    Bytes f;
    chunk(f, "FUJI", 0, {});
    const uint32_t header = static_cast<uint32_t>(f.size());
    chunk(f, "fsk ", 40, {10, 0, 20, 0});
    chunk(f, "fsk ", 0, {30, 0});
    chunk(f, "data", 5, fill(3, 1));
    MemoryTapeSource src({f.data(), f.size()});
    Bus bus;
    Player player(f, bus);
    FujiTapeLoop loop(src, bus, player);

    const FujiStep step = loop.step();

    TEST_ASSERT_EQUAL(FujiStepKind::Fsk, step.kind);
    TEST_ASSERT_EQUAL(2, bus.events.size());
    TEST_ASSERT_EQUAL(Ev::Wait, bus.events[0].kind);
    TEST_ASSERT_EQUAL_UINT32(40, bus.events[0].value);
    TEST_ASSERT_EQUAL(Ev::Fsk, bus.events[1].kind);
    TEST_ASSERT_EQUAL_UINT32(header, bus.events[1].value);
    TEST_ASSERT_EQUAL_UINT32(header + 12 + 10, step.offset);

    player.fail = true;
    const FujiStep data = loop.step();
    TEST_ASSERT_EQUAL(FujiStepKind::Data, data.kind);  // the data record is not an fsk run

    // A run that does not play, after a record: back to the end of that record, where the step started.
    Bytes g;
    chunk(g, "FUJI", 0, {});
    chunk(g, "data", 0, fill(3, 1));
    const uint32_t afterRecord = static_cast<uint32_t>(g.size());
    chunk(g, "baud", 600, {});
    chunk(g, "fsk ", 0, {10, 0});
    MemoryTapeSource gsrc({g.data(), g.size()});
    Player failing(g, bus);
    failing.fail = true;
    FujiTapeLoop second(gsrc, bus, failing);
    TEST_ASSERT_EQUAL(FujiStepKind::Data, second.step().kind);
    const FujiStep failed = second.step();
    TEST_ASSERT_EQUAL(FujiStepKind::Replay, failed.kind);
    TEST_ASSERT_EQUAL_UINT32(afterRecord, failed.offset);
    TEST_ASSERT_EQUAL_UINT32(afterRecord, second.offset());
}

// No record before the end of the file: offset 0 (the beta then disables the cassette).
void test_end_of_tape_goes_back_to_offset_zero()
{
    Bytes f;
    chunk(f, "FUJI", 0, {});
    chunk(f, "data", 0, fill(2, 0));
    chunk(f, "baud", 600, {});
    MemoryTapeSource src({f.data(), f.size()});
    Bus bus;
    Player player(f, bus);
    FujiTapeLoop loop(src, bus, player);

    TEST_ASSERT_EQUAL(FujiStepKind::Data, loop.step().kind);
    const FujiStep last = loop.step();
    TEST_ASSERT_EQUAL(FujiStepKind::End, last.kind);
    TEST_ASSERT_EQUAL_UINT32(0, last.offset);
    TEST_ASSERT_EQUAL_UINT32(0, loop.offset());
}

// A record cut by the end of the file sends nothing and ends the tape (a difference with the beta,
// documented: it does not send a stale buffer).
void test_cut_record_sends_nothing_and_ends()
{
    Bytes f;
    chunk(f, "FUJI", 0, {});
    chunk(f, "data", 0, fill(10, 0));
    f.resize(f.size() - 4);
    MemoryTapeSource src({f.data(), f.size()});
    Bus bus;
    Player player(f, bus);
    FujiTapeLoop loop(src, bus, player);

    const FujiStep step = loop.step();
    TEST_ASSERT_EQUAL(FujiStepKind::End, step.kind);
    for (const Event& e : bus.events) {
        TEST_ASSERT_TRUE(e.kind != Ev::Write);
    }
}

// Review of PR #122, H3: the bus says stop (line wanted back, or it stopped taking bytes) in a write, a
// flush or a rate change. The step stops there and the record plays again from its start: nothing of it
// is skipped, nothing after it is sent.
void test_a_bus_that_says_stop_replays_the_record()
{
    Bytes f;
    chunk(f, "FUJI", 0, {});
    chunk(f, "data", 0, {1, 2});
    chunk(f, "baud", 600, {});  // a rate change before the second record
    chunk(f, "data", 0, fill(600, 0));
    MemoryTapeSource src({f.data(), f.size()});
    Bus bus;
    Player player(f, bus);
    FujiTapeLoop loop(src, bus, player);

    TEST_ASSERT_EQUAL(FujiStepKind::Data, loop.step().kind);
    const uint32_t second = loop.offset();
    bus.refuseWriteAt = 3;  // the second piece (256 + 256 + 88) of the second record
    FujiStep step = loop.step();
    TEST_ASSERT_EQUAL(FujiStepKind::Replay, step.kind);
    TEST_ASSERT_EQUAL_UINT32(second, step.offset);
    TEST_ASSERT_EQUAL_UINT32(second, loop.offset());
    TEST_ASSERT_EQUAL(3, bus.writes);  // 1 of the first record, 2 of this one: nothing after the refused one
    TEST_ASSERT_EQUAL(Ev::Write, bus.events.back().kind);

    bus.refuseFlushAt = bus.flushes + 1;
    TEST_ASSERT_EQUAL(FujiStepKind::Replay, loop.step().kind);
    TEST_ASSERT_EQUAL_UINT32(second, loop.offset());
    TEST_ASSERT_EQUAL(Ev::Flush, bus.events.back().kind);

    bus.refuseBaud = true;
    TEST_ASSERT_EQUAL(FujiStepKind::Replay, loop.step().kind);
    TEST_ASSERT_EQUAL_UINT32(second, loop.offset());
    TEST_ASSERT_EQUAL(Ev::Baud, bus.events.back().kind);
    TEST_ASSERT_FALSE(loop.enable());

    bus.refuseBaud = false;
    bus.events.clear();
    step = loop.step();
    TEST_ASSERT_EQUAL(FujiStepKind::Data, step.kind);  // the whole record, from its first byte
    TEST_ASSERT_EQUAL_UINT8(0, bus.events[1].bytes[0]);
}

// The line wanted back in the last second of a gap: MOTOR's 1000 ms grace does not apply, the step stops
// at the next millisecond and nothing is sent.
void test_stop_wanted_in_the_last_second_of_the_gap_stops_at_once()
{
    Bytes f;
    chunk(f, "FUJI", 0, {});
    chunk(f, "data", 1500, {7});
    MemoryTapeSource src({f.data(), f.size()});
    Bus bus;
    bus.stopAfterMs = 1200;  // 300 ms of gap left
    Player player(f, bus);
    FujiTapeLoop loop(src, bus, player);
    const FujiStep step = loop.step();
    TEST_ASSERT_EQUAL(FujiStepKind::Replay, step.kind);
    TEST_ASSERT_EQUAL_UINT32(1200, bus.waited);
    for (const Event& e : bus.events) {
        TEST_ASSERT_TRUE(e.kind != Ev::Write);
    }
}

namespace {

// Plays a whole real tape and checks each step against CasIndex's segment of the same file.
void checkRealTape(const char* name, unsigned minData, unsigned minFsk)
{
    const char* samples = std::getenv("RB_CAS_SAMPLES");
    if (samples == nullptr) {
        TEST_IGNORE_MESSAGE("RB_CAS_SAMPLES not set");
    }
    Bytes f;
    if (!readFile(std::string(samples) + "/" + name, f)) {
        TEST_IGNORE_MESSAGE("tape not found");
    }
    MemoryTapeSource src({f.data(), f.size()});
    std::vector<rb::cassette::CasSegment> table(4096);
    const auto built = rb::cassette::CasIndex::build(src, {table.data(), table.size()});
    TEST_ASSERT_EQUAL(rb::cassette::CasStatus::Ok, built.status);

    Bus bus;
    Player player(f, bus);
    FujiTapeLoop loop(src, bus, player);
    loop.enable();
    uint32_t baud = 600;
    unsigned data = 0;
    unsigned fsk = 0;
    for (uint32_t i = 0; i < built.count; ++i) {
        const rb::cassette::CasSegment& seg = table[i];
        bus.events.clear();
        const FujiStep step = loop.step();
        uint32_t waitedMs = 0;
        Bytes sent;
        for (const Event& e : bus.events) {
            if (e.kind == Ev::Baud) {
                baud = e.value;
            } else if (e.kind == Ev::Wait) {
                waitedMs += e.value;
            } else if (e.kind == Ev::Write) {
                TEST_ASSERT_TRUE(e.bytes.size() <= 256);
                sent.insert(sent.end(), e.bytes.begin(), e.bytes.end());
            }
        }
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(seg.gapMs, waitedMs, "gap");
        if (seg.kind == rb::cassette::SegmentKind::Data) {
            ++data;
            TEST_ASSERT_EQUAL_MESSAGE(FujiStepKind::Data, step.kind, "data step");
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(seg.baud, baud, "baud");
            TEST_ASSERT_EQUAL_UINT32(seg.length, sent.size());
            TEST_ASSERT_TRUE(std::equal(sent.begin(), sent.end(), f.begin() + seg.offset));
            TEST_ASSERT_EQUAL_UINT32(seg.offset + seg.length, step.offset);
        } else {
            ++fsk;
            TEST_ASSERT_EQUAL_MESSAGE(FujiStepKind::Fsk, step.kind, "fsk step");
            TEST_ASSERT_TRUE(sent.empty());
            TEST_ASSERT_EQUAL(Ev::Fsk, bus.events.back().kind);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(seg.offset, bus.events.back().value, "fsk header");
        }
    }
    TEST_ASSERT_EQUAL_MESSAGE(FujiStepKind::End, loop.step().kind, "end of tape");
    TEST_ASSERT_TRUE(data >= minData);
    TEST_ASSERT_TRUE(fsk >= minFsk);
}

}  // namespace

// International Karate: 106 records at 600 and 750 baud and 107 FSK runs, in the order of the file.
void test_real_international_karate_step_by_step()
{
    checkRealTape("international_karate.cas", 106, 107);
}

// PAL Alien Ambush: records and FSK runs alternate.
void test_real_pal_alien_ambush_step_by_step()
{
    checkRealTape("PAL_ALIEN_AMBUSH_TURBO_SOFTWARE.cas", 1, 1);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_enable_sets_600_baud);
    RUN_TEST(test_data_record_goes_out_after_its_gap_in_pieces_of_256);
    RUN_TEST(test_motor_drop_early_in_the_gap_replays_from_the_step_start);
    RUN_TEST(test_motor_drop_in_the_last_second_of_the_gap_is_ignored);
    RUN_TEST(test_motor_drop_with_1001_ms_left_counts);
    RUN_TEST(test_fsk_run_goes_to_the_player_after_its_gap);
    RUN_TEST(test_end_of_tape_goes_back_to_offset_zero);
    RUN_TEST(test_cut_record_sends_nothing_and_ends);
    RUN_TEST(test_a_bus_that_says_stop_replays_the_record);
    RUN_TEST(test_stop_wanted_in_the_last_second_of_the_gap_stops_at_once);
    RUN_TEST(test_real_international_karate_step_by_step);
    RUN_TEST(test_real_pal_alien_ambush_step_by_step);
    return UNITY_END();
}
