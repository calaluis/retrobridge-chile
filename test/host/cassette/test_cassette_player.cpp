// CassettePlayer (spec cassette-cas, C104, AC5): records paced by the clock with MOTOR
// (the C002 cases, kept), FSK runs through the loader and the start gate, a MOTOR drop in
// a run stops the signal and replays the run, no allocation. The whole-tape case plays the
// real PAL Alien Ambush (records and FSK runs alternate) from RB_CAS_SAMPLES.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "rb/cassette/cas_index.hpp"
#include "rb/cassette/cassette_player.hpp"
#include "rb/cassette/fsk_loader.hpp"
#include "unity.h"

using rb::cassette::ByteOut;
using rb::cassette::CasIndex;
using rb::cassette::CasIndexResult;
using rb::cassette::CasSegment;
using rb::cassette::CassettePlayer;
using rb::cassette::CasStatus;
using rb::cassette::FskLoader;
using rb::cassette::FskPortion;
using rb::cassette::FskWave;
using rb::cassette::LoaderState;
using rb::cassette::MemoryTapeSource;
using rb::cassette::MotorIn;
using rb::cassette::PlayerPhase;
using rb::cassette::SegmentKind;
using rb::cassette::TapeSource;
using rb::cassette::WaveOut;
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

Bytes fuji() { return chunk("FUJI", 0, {}); }

Bytes values(std::initializer_list<uint16_t> list)
{
    Bytes out;
    for (uint16_t v : list) {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>(v >> 8));
    }
    return out;
}

Bytes repeat(std::size_t count, uint16_t units)
{
    Bytes out;
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(static_cast<uint8_t>(units & 0xFF));
        out.push_back(static_cast<uint8_t>(units >> 8));
    }
    return out;
}

class FakeMotor final : public MotorIn {
public:
    bool motor = true;
    bool on() const override { return motor; }
};

class FakeBytes final : public ByteOut {
public:
    bool failSend = false;
    std::vector<uint32_t> bauds;
    Bytes sent;
    std::size_t largestSend = 0;

    bool setBaud(uint32_t baud) override
    {
        bauds.push_back(baud);
        return true;
    }
    bool send(Span<const uint8_t> bytes) override
    {
        if (failSend) {
            return false;
        }
        largestSend = bytes.size() > largestSend ? bytes.size() : largestSend;
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            sent.push_back(bytes[i]);
        }
        return true;
    }
};

// A queue the test drains by hand (the RMT in firmware).
class FakeWave final : public WaveOut {
public:
    std::size_t capacity = 1u << 30;
    std::size_t pending = 0;
    std::vector<FskPortion> pushed;
    unsigned stops = 0;
    bool autoDrain = false;  // play everything at once

    bool push(const FskPortion& portion) override
    {
        if (pending >= capacity) {
            return false;
        }
        pushed.push_back(portion);
        pending = autoDrain ? 0 : pending + 1;
        return true;
    }
    bool idle() const override { return pending == 0; }
    void stop() override
    {
        ++stops;
        pending = 0;
    }
    uint64_t ticks() const
    {
        uint64_t total = 0;
        for (const FskPortion& p : pushed) {
            total += p.ticks;
        }
        return total;
    }
};

// A source whose reads can be made to fail.
class FlakySource final : public TapeSource {
public:
    explicit FlakySource(const Bytes& bytes) : inner(Span<const uint8_t>(bytes.data(), bytes.size())) {}
    uint32_t size() const override { return inner.size(); }
    bool read(uint32_t offset, Span<uint8_t> dst) override { return !fail && inner.read(offset, dst); }
    MemoryTapeSource inner;
    bool fail = false;
};

constexpr uint64_t kByte600 = 16667;  // 10 bits at 600 baud, rounded up
constexpr uint64_t kByte800 = 12500;  // 10 bits at 800 baud

CasSegment g_table[1024];
uint8_t g_bank[1u << 20];

struct Rig {
    Bytes file;
    FlakySource source;
    FskLoader loader;
    CassettePlayer player{loader};
    FakeMotor motor;
    FakeBytes bytes;
    FakeWave wave;
    uint32_t count = 0;
    uint64_t now = 0;
    bool pump = true;  // run the loader's producer on every advance

    explicit Rig(const Bytes& tape, std::size_t bank = sizeof g_bank) : file(tape), source(file)
    {
        const CasIndexResult index = CasIndex::build(source, Span<CasSegment>(g_table, 1024));
        TEST_ASSERT_EQUAL(static_cast<int>(CasStatus::Ok), static_cast<int>(index.status));
        count = index.count;
        loader.bind(Span<uint8_t>(g_bank, bank));
        player.load(source, Span<const CasSegment>(g_table, count), wave);
        player.tick(now, motor, bytes, wave);
    }
    void tickOnce()
    {
        if (pump) {
            for (int i = 0; i < 100000 && loader.step() == rb::cassette::LoaderStep::Progress; ++i) {
            }
        }
        player.tick(now, motor, bytes, wave);
    }
    void advance(uint64_t us)
    {
        now += us;
        tickOnce();
    }
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

}  // namespace

void setUp() {}
void tearDown() {}

// ---- records (C002 cases, same behaviour) ----

void test_gap_then_rate_then_paced_bytes()
{
    Rig rig(fuji() + chunk("data", 100, {0x55, 0x55, 0xFC}));
    rig.advance(99999);
    TEST_ASSERT_EQUAL_UINT32(0, rig.bytes.sent.size());
    rig.advance(1);  // gap over
    TEST_ASSERT_EQUAL_UINT32(1, rig.bytes.bauds.size());
    TEST_ASSERT_EQUAL_UINT32(600, rig.bytes.bauds[0]);
    TEST_ASSERT_EQUAL_UINT32(0, rig.bytes.sent.size());
    rig.advance(kByte600);
    TEST_ASSERT_EQUAL_UINT32(1, rig.bytes.sent.size());
    rig.advance(2 * kByte600);
    TEST_ASSERT_EQUAL_UINT32(3, rig.bytes.sent.size());
    TEST_ASSERT_EQUAL_UINT8(0xFC, rig.bytes.sent[2]);
    TEST_ASSERT_TRUE(rig.player.atEnd());
}

void test_rate_change_between_records()
{
    Rig rig(fuji() + chunk("data", 0, {1}) + chunk("baud", 800, {}) + chunk("data", 0, {2, 3}) +
            chunk("data", 0, {4}));
    rig.advance(kByte600);
    TEST_ASSERT_EQUAL_UINT32(1, rig.bytes.sent.size());
    rig.advance(kByte800);
    TEST_ASSERT_EQUAL_UINT32(2, rig.bytes.sent.size());
    rig.advance(2 * kByte800);
    TEST_ASSERT_EQUAL_UINT32(4, rig.bytes.sent.size());
    TEST_ASSERT_EQUAL_UINT32(2, rig.bytes.bauds.size());
    TEST_ASSERT_EQUAL_UINT32(800, rig.bytes.bauds[1]);
}

// AC5: MOTOR off in the middle of a record freezes it; on again, it goes on from there.
void test_motor_off_freezes_a_record_and_resumes()
{
    Rig rig(fuji() + chunk("data", 0, {1, 2, 3, 4}));
    rig.advance(2 * kByte600);
    TEST_ASSERT_EQUAL_UINT32(2, rig.bytes.sent.size());
    rig.motor.motor = false;
    rig.advance(10000000);
    TEST_ASSERT_EQUAL_UINT32(2, rig.bytes.sent.size());
    rig.motor.motor = true;
    rig.advance(1);  // the stopped time does not count
    TEST_ASSERT_EQUAL_UINT32(2, rig.bytes.sent.size());
    rig.advance(kByte600);
    TEST_ASSERT_EQUAL_UINT32(3, rig.bytes.sent.size());
    TEST_ASSERT_EQUAL_UINT8(3, rig.bytes.sent[2]);
}

void test_motor_off_freezes_the_gap()
{
    Rig rig(fuji() + chunk("data", 50, {7}));
    rig.advance(30000);
    rig.motor.motor = false;
    rig.advance(60000);
    rig.motor.motor = true;
    rig.advance(500);  // the tick that sees MOTOR on again counts nothing
    rig.advance(19999);
    TEST_ASSERT_EQUAL_UINT32(0, rig.bytes.bauds.size());
    rig.advance(1 + kByte600);
    TEST_ASSERT_EQUAL_UINT32(1, rig.bytes.sent.size());
}

void test_end_of_tape_sends_nothing_more()
{
    Rig rig(fuji() + chunk("data", 0, {1}));
    rig.advance(kByte600);
    rig.advance(60000000);
    TEST_ASSERT_EQUAL_UINT32(1, rig.bytes.sent.size());
    TEST_ASSERT_TRUE(rig.player.atEnd());
    TEST_ASSERT_EQUAL(static_cast<int>(PlayerPhase::End), static_cast<int>(rig.player.phase()));
}

void test_failed_send_does_not_move_the_tape()
{
    Rig rig(fuji() + chunk("data", 0, {9, 8}));
    rig.bytes.failSend = true;
    rig.advance(kByte600);
    TEST_ASSERT_EQUAL_UINT32(0, rig.bytes.sent.size());
    rig.bytes.failSend = false;
    rig.advance(1);
    TEST_ASSERT_EQUAL_UINT32(1, rig.bytes.sent.size());
    TEST_ASSERT_EQUAL_UINT8(9, rig.bytes.sent[0]);
}

// A failed read of the tape does not move it either.
void test_failed_read_does_not_move_the_tape()
{
    Rig rig(fuji() + chunk("data", 0, {9, 8}));
    rig.source.fail = true;
    rig.advance(kByte600);
    TEST_ASSERT_EQUAL_UINT32(0, rig.bytes.sent.size());
    rig.source.fail = false;
    rig.advance(1);
    TEST_ASSERT_EQUAL_UINT32(1, rig.bytes.sent.size());
    TEST_ASSERT_EQUAL_UINT8(9, rig.bytes.sent[0]);
}

void test_sends_are_at_most_512_bytes()
{
    Rig rig(fuji() + chunk("data", 0, Bytes(1000, 0xAA)));
    rig.advance(1000 * kByte600);
    TEST_ASSERT_EQUAL_UINT32(1000, rig.bytes.sent.size());
    TEST_ASSERT_TRUE(rig.bytes.largestSend <= 512);
}

void test_pacing_does_not_drift()
{
    Rig rig(fuji() + chunk("data", 0, Bytes(600, 0x11)));
    for (int i = 0; i < 100; ++i) {
        rig.advance(99999);
    }
    TEST_ASSERT_EQUAL_UINT32(599, rig.bytes.sent.size());
    rig.advance(100);
    TEST_ASSERT_EQUAL_UINT32(600, rig.bytes.sent.size());
}

void test_refused_time_is_not_sent_as_a_burst()
{
    Rig rig(fuji() + chunk("data", 0, Bytes(100, 0x22)));
    rig.bytes.failSend = true;
    rig.advance(1000000);
    rig.bytes.failSend = false;
    rig.advance(1);
    TEST_ASSERT_EQUAL_UINT32(1, rig.bytes.sent.size());
    rig.advance(kByte600);
    TEST_ASSERT_EQUAL_UINT32(2, rig.bytes.sent.size());
}

void test_clock_going_back_counts_nothing()
{
    Rig rig(fuji() + chunk("data", 100, Bytes(10, 0x33)));
    rig.advance(100000 + kByte600);
    TEST_ASSERT_EQUAL_UINT32(1, rig.bytes.sent.size());
    rig.now -= 5 * kByte600;
    rig.tickOnce();
    rig.now += 5 * kByte600;
    rig.tickOnce();
    TEST_ASSERT_EQUAL_UINT32(1, rig.bytes.sent.size());
}

void test_rewind_restarts_the_tape()
{
    Rig rig(fuji() + chunk("data", 0, {1, 2}));
    rig.advance(2 * kByte600);
    TEST_ASSERT_TRUE(rig.player.atEnd());
    rig.player.rewind(rig.wave);
    rig.advance(kByte600);
    TEST_ASSERT_EQUAL_UINT32(3, rig.bytes.sent.size());
    TEST_ASSERT_EQUAL_UINT8(1, rig.bytes.sent[2]);
    TEST_ASSERT_EQUAL_UINT32(2, rig.bytes.bauds.size());  // the rate is sent again
}

// ---- FSK runs ----

// The gap passes, then the run's portions go to WaveOut as the file says; when they have
// played, the next record starts.
void test_fsk_run_plays_after_its_gap()
{
    Rig rig(fuji() + chunk("fsk ", 10, values({3, 5, 400})) + chunk("data", 0, {7}));
    rig.advance(9999);
    TEST_ASSERT_EQUAL_size_t(0, rig.wave.pushed.size());
    rig.advance(1);
    TEST_ASSERT_EQUAL_size_t(4, rig.wave.pushed.size());  // 300, 500, 32767 + 7233
    TEST_ASSERT_FALSE(rig.wave.pushed[0].level);
    TEST_ASSERT_TRUE(rig.wave.pushed[1].level);
    TEST_ASSERT_EQUAL_UINT32(32767, rig.wave.pushed[2].ticks);
    TEST_ASSERT_EQUAL_UINT64(40800, rig.wave.ticks());
    TEST_ASSERT_EQUAL(static_cast<int>(PlayerPhase::FskDrain), static_cast<int>(rig.player.phase()));
    rig.advance(kByte600);
    TEST_ASSERT_EQUAL_UINT32(0, rig.bytes.sent.size());  // still playing
    rig.wave.pending = 0;
    rig.advance(1);
    rig.advance(kByte600);
    TEST_ASSERT_EQUAL_UINT32(1, rig.bytes.sent.size());
}

// Nothing is pushed until the loader's start gate opens.
void test_fsk_waits_for_the_start_gate()
{
    Rig rig(fuji() + chunk("fsk ", 0, repeat(2000, 300)));
    rig.pump = false;
    rig.advance(1000);
    TEST_ASSERT_EQUAL(static_cast<int>(PlayerPhase::FskWait), static_cast<int>(rig.player.phase()));
    TEST_ASSERT_EQUAL_size_t(0, rig.wave.pushed.size());
    for (int i = 0; i < 6; ++i) {
        (void)rig.loader.step();  // header + 5 blocks: 1280 values, gate still closed
    }
    rig.advance(1);
    TEST_ASSERT_EQUAL_size_t(0, rig.wave.pushed.size());
    (void)rig.loader.step();  // 1536 values: open
    rig.advance(1);
    TEST_ASSERT_EQUAL_size_t(1536, rig.wave.pushed.size());
}

// The player never pushes what the loader has not published; the rest follows.
void test_fsk_follows_the_loader()
{
    Rig rig(fuji() + chunk("fsk ", 0, repeat(3000, 300)));
    rig.pump = false;
    for (int i = 0; i < 7; ++i) {
        (void)rig.loader.step();
    }
    rig.advance(1);
    TEST_ASSERT_EQUAL_size_t(1536, rig.wave.pushed.size());
    (void)rig.loader.step();
    rig.advance(1);
    TEST_ASSERT_EQUAL_size_t(1792, rig.wave.pushed.size());
    rig.pump = true;
    rig.advance(1);
    TEST_ASSERT_EQUAL_size_t(3000, rig.wave.pushed.size());
}

// A full queue loses nothing: refused portions are pushed again, in order.
void test_full_wave_queue_loses_nothing()
{
    Rig rig(fuji() + chunk("fsk ", 0, values({1, 2, 3, 4, 5})));
    rig.wave.capacity = 2;
    rig.advance(1);
    TEST_ASSERT_EQUAL_size_t(2, rig.wave.pushed.size());
    rig.wave.pending = 0;
    rig.advance(1);
    rig.wave.pending = 0;
    rig.advance(1);
    TEST_ASSERT_EQUAL_size_t(5, rig.wave.pushed.size());
    for (uint32_t i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_UINT32((i + 1) * 100, rig.wave.pushed[i].ticks);
        TEST_ASSERT_EQUAL((i & 1u) != 0, rig.wave.pushed[i].level);
    }
}

// AC5: MOTOR drops during a run: the signal stops at once (DATA IN idle), the loader
// stops; MOTOR on again: the run's gap, then the whole run again from its start.
void test_motor_drop_in_a_run_stops_and_replays_it()
{
    Rig rig(fuji() + chunk("fsk ", 20, values({3, 5, 7})) + chunk("data", 0, {9}));
    rig.advance(20000);
    TEST_ASSERT_EQUAL_size_t(3, rig.wave.pushed.size());
    rig.motor.motor = false;
    rig.advance(1000);
    TEST_ASSERT_EQUAL_UINT32(1, rig.wave.stops);
    TEST_ASSERT_EQUAL_UINT32(1, rig.player.fskRestarts());
    TEST_ASSERT_EQUAL(static_cast<int>(PlayerPhase::Gap), static_cast<int>(rig.player.phase()));
    TEST_ASSERT_EQUAL_UINT32(0, rig.player.segment());
    rig.motor.motor = true;
    rig.advance(1);       // counts nothing
    rig.advance(19999);   // the gap again, one us short
    TEST_ASSERT_EQUAL_size_t(3, rig.wave.pushed.size());
    rig.advance(1);
    TEST_ASSERT_EQUAL_size_t(6, rig.wave.pushed.size());
    TEST_ASSERT_EQUAL_UINT32(300, rig.wave.pushed[3].ticks);  // from its first value
    TEST_ASSERT_FALSE(rig.wave.pushed[3].level);
}

// MOTOR off while the run waits for its start gate: nothing has played, so nothing is cut;
// the loader keeps loading (as the owner's preload) and nothing is pushed until MOTOR is on.
void test_motor_off_while_waiting_keeps_loading()
{
    Rig rig(fuji() + chunk("fsk ", 0, repeat(2000, 300)));
    rig.pump = false;
    rig.advance(1);
    TEST_ASSERT_EQUAL(static_cast<int>(PlayerPhase::FskWait), static_cast<int>(rig.player.phase()));
    rig.motor.motor = false;
    rig.pump = true;
    rig.advance(1);
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderState::Final), static_cast<int>(rig.loader.state()));
    TEST_ASSERT_EQUAL_size_t(0, rig.wave.pushed.size());
    rig.motor.motor = true;
    rig.advance(1);
    TEST_ASSERT_EQUAL_size_t(2000, rig.wave.pushed.size());
    TEST_ASSERT_EQUAL_UINT32(0, rig.wave.stops);
    TEST_ASSERT_EQUAL_UINT32(0, rig.player.fskRestarts());
}

// The loader fails after the signal started (4 failed reads in a row): fail safe, as the
// owner's: the signal stops and the same run plays again from its first value.
void test_loader_failure_mid_run_replays_the_run()
{
    Rig rig(fuji() + chunk("fsk ", 0, repeat(3000, 300)));
    rig.pump = false;
    for (int i = 0; i < 8; ++i) {
        (void)rig.loader.step();  // header + 7 blocks: 1792 values, gate open
    }
    rig.advance(1);
    TEST_ASSERT_EQUAL_size_t(1792, rig.wave.pushed.size());
    rig.source.fail = true;
    for (int i = 0; i < 4; ++i) {
        (void)rig.loader.step();
    }
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderState::Failed), static_cast<int>(rig.loader.state()));
    rig.advance(1);
    TEST_ASSERT_EQUAL_UINT32(1, rig.wave.stops);
    TEST_ASSERT_EQUAL_UINT32(1, rig.player.fskRestarts());
    rig.source.fail = false;
    rig.pump = true;
    rig.advance(1);  // the loader starts again
    rig.advance(1);  // loaded: the whole run from its first value
    TEST_ASSERT_EQUAL_size_t(1792 + 3000, rig.wave.pushed.size());
    TEST_ASSERT_EQUAL_UINT32(30000, rig.wave.pushed[1792].ticks);
    TEST_ASSERT_FALSE(rig.wave.pushed[1792].level);
}

// After a cut, the player does not restart the loader while its producer has not stopped
// (start() is not thread-safe with step()): the pending stop is still seen by the producer.
void test_restart_waits_for_the_producer_to_stop()
{
    Rig rig(fuji() + chunk("fsk ", 0, repeat(3000, 300)));
    rig.pump = false;
    for (int i = 0; i < 8; ++i) {
        (void)rig.loader.step();
    }
    rig.advance(1);
    rig.motor.motor = false;
    rig.advance(1);  // cut: stop requested, the producer has not run yet
    rig.motor.motor = true;
    rig.advance(1);
    rig.advance(1);
    TEST_ASSERT_EQUAL(static_cast<int>(LoaderState::Loading), static_cast<int>(rig.loader.state()));
    TEST_ASSERT_EQUAL(static_cast<int>(rb::cassette::LoaderStep::Stopped), static_cast<int>(rig.loader.step()));
    rig.pump = true;
    rig.advance(1);  // now the player starts it
    rig.advance(1);
    TEST_ASSERT_EQUAL_size_t(1792 + 3000, rig.wave.pushed.size());
}

// MOTOR drops in the same tick the run finished playing: nothing is replayed.
void test_motor_drop_after_the_run_played_does_not_replay()
{
    Rig rig(fuji() + chunk("fsk ", 0, values({3, 5})) + chunk("data", 0, {7}));
    rig.advance(1);
    TEST_ASSERT_EQUAL(static_cast<int>(PlayerPhase::FskDrain), static_cast<int>(rig.player.phase()));
    rig.wave.pending = 0;
    rig.motor.motor = false;
    rig.advance(1);
    TEST_ASSERT_EQUAL_UINT32(0, rig.player.fskRestarts());
    TEST_ASSERT_EQUAL_UINT32(0, rig.wave.stops);
    TEST_ASSERT_EQUAL_UINT32(1, rig.player.segment());
}

// A run that does not fit the bank stops the tape there: nothing pushed, nothing skipped.
void test_run_too_big_for_the_bank_stops_the_tape()
{
    Rig rig(fuji() + chunk("fsk ", 0, repeat(600, 9)) + chunk("data", 0, {1}), 512);
    rig.advance(1);
    rig.advance(10 * kByte600);
    TEST_ASSERT_TRUE(rig.player.blocked());
    TEST_ASSERT_EQUAL_size_t(0, rig.wave.pushed.size());
    TEST_ASSERT_EQUAL_UINT32(0, rig.bytes.sent.size());
    TEST_ASSERT_EQUAL_UINT32(0, rig.player.segment());
}

// Rewind during a run stops the signal and starts the tape again.
void test_rewind_during_a_run_stops_the_signal()
{
    Rig rig(fuji() + chunk("fsk ", 0, values({3, 5})));
    rig.advance(1);
    TEST_ASSERT_EQUAL_size_t(2, rig.wave.pushed.size());
    rig.player.rewind(rig.wave);
    TEST_ASSERT_EQUAL_UINT32(1, rig.wave.stops);
    TEST_ASSERT_EQUAL_UINT32(0, rig.player.segment());
    rig.advance(1);  // the loader starts again
    rig.advance(1);  // loaded: plays from the first value
    TEST_ASSERT_EQUAL_size_t(4, rig.wave.pushed.size());
    TEST_ASSERT_EQUAL_UINT32(300, rig.wave.pushed[2].ticks);
}

// ADR-011: playing records and runs allocates nothing.
void test_no_allocation_while_playing()
{
    Rig rig(fuji() + chunk("data", 5, Bytes(50, 0x11)) + chunk("fsk ", 5, repeat(100, 7)) + chunk("data", 0, {1}));
    rig.wave.autoDrain = true;
    rig.wave.pushed.reserve(1000);
    rig.bytes.sent.reserve(1000);
    rig.bytes.bauds.reserve(10);
    const unsigned long before = g_allocations;
    for (int i = 0; i < 200 && !rig.player.atEnd(); ++i) {
        rig.advance(kByte600);
    }
    TEST_ASSERT_TRUE(rig.player.atEnd());
    TEST_ASSERT_EQUAL_UINT32(before, g_allocations);
    int* probe = new int(1);  // positive control
    TEST_ASSERT_EQUAL_UINT32(before + 1, g_allocations);
    delete probe;
}

// AC5 on a real tape: PAL Alien Ambush (records and FSK runs alternate) plays to the end:
// every record byte and every FSK tick of the file, in order, MOTOR on throughout.
void test_real_alien_ambush_plays_to_the_end()
{
    const char* samples = std::getenv("RB_CAS_SAMPLES");
    if (samples == nullptr) {
        TEST_IGNORE_MESSAGE("RB_CAS_SAMPLES not set");
    }
    Bytes file;
    if (!readFile(std::string(samples) + "/PAL_ALIEN_AMBUSH_TURBO_SOFTWARE.cas", file)) {
        TEST_IGNORE_MESSAGE("PAL_ALIEN_AMBUSH_TURBO_SOFTWARE.cas not found");
    }
    Rig rig(file);
    rig.wave.autoDrain = true;
    Bytes expectedBytes;
    uint64_t expectedTicks = 0;
    uint32_t runs = 0;
    for (uint32_t s = 0; s < rig.count; ++s) {
        const CasSegment& seg = g_table[s];
        if (seg.kind == SegmentKind::Data) {
            expectedBytes.insert(expectedBytes.end(), file.begin() + seg.offset, file.begin() + seg.offset + seg.length);
            continue;
        }
        ++runs;
        uint32_t at = seg.offset;
        for (uint8_t c = 0; c < seg.chunks; ++c) {
            const uint16_t length = static_cast<uint16_t>(file[at + 4] | (file[at + 5] << 8));
            expectedTicks += FskWave::sumTicks(Span<const uint8_t>(file.data() + at + 8, length));
            at += 8u + length;
        }
    }
    for (int i = 0; i < 100000 && !rig.player.atEnd(); ++i) {
        rig.advance(100000);
    }
    TEST_ASSERT_TRUE(rig.player.atEnd());
    TEST_ASSERT_EQUAL_size_t(expectedBytes.size(), rig.bytes.sent.size());
    TEST_ASSERT_TRUE(expectedBytes == rig.bytes.sent);
    TEST_ASSERT_EQUAL_UINT64(expectedTicks, rig.wave.ticks());
    std::printf("alien ambush: %u segments, %u fsk runs, %u record bytes, %llu us of fsk, tape %llu s\n",
                static_cast<unsigned>(rig.count), static_cast<unsigned>(runs),
                static_cast<unsigned>(rig.bytes.sent.size()), static_cast<unsigned long long>(expectedTicks),
                static_cast<unsigned long long>(rig.now / 1000000ULL));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_gap_then_rate_then_paced_bytes);
    RUN_TEST(test_rate_change_between_records);
    RUN_TEST(test_motor_off_freezes_a_record_and_resumes);
    RUN_TEST(test_motor_off_freezes_the_gap);
    RUN_TEST(test_end_of_tape_sends_nothing_more);
    RUN_TEST(test_failed_send_does_not_move_the_tape);
    RUN_TEST(test_failed_read_does_not_move_the_tape);
    RUN_TEST(test_sends_are_at_most_512_bytes);
    RUN_TEST(test_pacing_does_not_drift);
    RUN_TEST(test_refused_time_is_not_sent_as_a_burst);
    RUN_TEST(test_clock_going_back_counts_nothing);
    RUN_TEST(test_rewind_restarts_the_tape);
    RUN_TEST(test_fsk_run_plays_after_its_gap);
    RUN_TEST(test_fsk_waits_for_the_start_gate);
    RUN_TEST(test_fsk_follows_the_loader);
    RUN_TEST(test_full_wave_queue_loses_nothing);
    RUN_TEST(test_motor_drop_in_a_run_stops_and_replays_it);
    RUN_TEST(test_motor_off_while_waiting_keeps_loading);
    RUN_TEST(test_loader_failure_mid_run_replays_the_run);
    RUN_TEST(test_restart_waits_for_the_producer_to_stop);
    RUN_TEST(test_motor_drop_after_the_run_played_does_not_replay);
    RUN_TEST(test_run_too_big_for_the_bank_stops_the_tape);
    RUN_TEST(test_rewind_during_a_run_stops_the_signal);
    RUN_TEST(test_no_allocation_while_playing);
    RUN_TEST(test_real_alien_ambush_plays_to_the_end);
    return UNITY_END();
}
