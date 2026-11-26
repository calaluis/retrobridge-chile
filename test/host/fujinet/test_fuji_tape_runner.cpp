// FujiTapeRunner (cassette-motor-fujinet): FujiTapeLoop driven from the cassette task. The enable rules are
// RetroBridge's assumptions (docs/diseno/cassette-motor-fujinet.md §3): MOTOR on enables at 600 baud, MOTOR
// off between records keeps the place, the end of the tape waits for MOTOR off and on, a new mount, eject or
// rewind starts over. Review of PR #122: shared DATA IN with the disk's handover (H1), the end published and
// `cas status` from the runner (H2), every wait bounded and stoppable (H3).
#include <vector>

#include "rb/cassette/cassette_player.hpp"
#include "rb/cassette/cassette_tick.hpp"
#include "rb/cassette/tape_source.hpp"
#include "rb/fujinet/fuji_service_rules.hpp"
#include "rb/fujinet/fuji_tape_runner.hpp"
#include "unity.h"

using rb::cassette::DataInLender;
using rb::cassette::LiveLine;
using rb::cassette::MemoryTapeSource;
using rb::fujinet::FujiStepKind;
using rb::fujinet::FujiTapeRunner;
using Bytes = std::vector<uint8_t>;

void setUp() {}
void tearDown() {}

namespace {

class Out final : public rb::cassette::ByteOut {
public:
    std::vector<uint32_t> bauds;
    Bytes sent;
    unsigned busySends = 0;          // send() refuses this many times first
    mutable unsigned undrained = 0;  // drained() is false this many times first
    unsigned refusals = 0;
    bool refuseAll = false;          // a line that never takes a byte again
    bool refuseBaud = false;
    std::size_t maxSend = 0;         // > 0: larger sends are refused (shared mode: < 32 in flight)
    std::size_t largest = 0;
    unsigned sends = 0;
    void (*onSend)(unsigned sends) = nullptr;

    bool setBaud(uint32_t baud) override
    {
        if (refuseBaud) {
            return false;
        }
        bauds.push_back(baud);
        return true;
    }
    bool send(rb::core::Span<const uint8_t> bytes) override
    {
        if (refuseAll || busySends > 0 || (maxSend > 0 && bytes.size() > maxSend)) {
            if (busySends > 0) {
                --busySends;
            }
            ++refusals;
            return false;
        }
        sent.insert(sent.end(), bytes.data(), bytes.data() + bytes.size());
        largest = bytes.size() > largest ? bytes.size() : largest;
        ++sends;
        if (onSend != nullptr) {
            onSend(sends);
        }
        return true;
    }
    bool drained() const override
    {
        if (undrained > 0) {
            --undrained;
            return false;
        }
        return true;
    }
};

class Motor final : public rb::cassette::MotorIn {
public:
    bool level = false;
    bool on() const override { return level; }
};

class Pause final : public rb::fujinet::Pause {
public:
    unsigned ms = 0;
    void waitOneMs() override { ++ms; }
};

class NoFsk final : public rb::fujinet::FskRunPlayer {
public:
    rb::fujinet::FskRunOutcome play(uint32_t, uint16_t) override { return {}; }
};

class Watch final : public rb::fujinet::StepWatch {
public:
    bool stop = false;
    unsigned asked = 0;
    bool stopWanted() noexcept override
    {
        ++asked;
        return stop;
    }
};

// The SIO task's side: what it asks, what the cassette confirmed.
class Lender final : public DataInLender {
public:
    Ask ask{Kind::Release, 0};
    uint32_t confirmedSeq = 0;
    Ack ack = Ack::Disk;
    unsigned confirms = 0;
    void request(Kind kind) { ask = Ask{kind, ask.seq + 1}; }
    Ask pending() const noexcept override { return ask; }
    void confirm(uint32_t seq, Ack a) noexcept override
    {
        confirmedSeq = seq;
        ack = a;
        ++confirms;
    }
    bool answered() const { return confirmedSeq == ask.seq; }
};

class Line final : public LiveLine {
public:
    bool free = true;
    GiveBack back = GiveBack::Uart;
    unsigned giveBacks = 0;
    bool hardwareFree() const noexcept override { return free; }
    GiveBack giveBack() noexcept override
    {
        ++giveBacks;
        return back;
    }
    void pump() noexcept override {}
};

class Sink final : public rb::cassette::EndSink {
public:
    std::vector<bool> seen;
    void publish(bool ended) noexcept override { seen.push_back(ended); }
    bool last() const { return !seen.empty() && seen.back(); }
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

Bytes twoRecords()
{
    Bytes f;
    chunk(f, "FUJI", 0, {});
    chunk(f, "data", 3, {1, 2, 3});
    chunk(f, "baud", 750, {});
    chunk(f, "data", 2, {4, 5});
    return f;
}

Bytes fill(std::size_t n, uint8_t first)
{
    Bytes b(n);
    for (std::size_t i = 0; i < n; ++i) {
        b[i] = static_cast<uint8_t>(first + i);
    }
    return b;
}

// A long record (600 bytes) after a short one, at 750 baud.
Bytes longRecord()
{
    Bytes f;
    chunk(f, "FUJI", 0, {});
    chunk(f, "baud", 750, {});
    chunk(f, "data", 0, {9});
    chunk(f, "data", 0, fill(600, 0));
    return f;
}

struct Rig {
    Bytes file;
    MemoryTapeSource src;
    Out out;
    Motor motor;
    Pause pause;
    NoFsk fsk;
    FujiTapeRunner runner{out, motor, fsk, pause};
    explicit Rig(Bytes f) : file(std::move(f)), src({file.data(), file.size()}) {}
};

}  // namespace

// No tape, or MOTOR off: nothing plays and nothing is touched.
void test_nothing_plays_without_tape_or_motor()
{
    Rig r(twoRecords());
    r.motor.level = true;
    TEST_ASSERT_FALSE(r.runner.tick());  // no tape yet
    TEST_ASSERT_TRUE(r.runner.follow(&r.src, 1));
    r.motor.level = false;
    TEST_ASSERT_FALSE(r.runner.tick());
    TEST_ASSERT_TRUE(r.out.bauds.empty());
    TEST_ASSERT_TRUE(r.out.sent.empty());
}

// MOTOR on: enabled at 600 baud, one record per tick, in order; the end waits for MOTOR off and on, then the
// tape starts again from its first record. `cas status` follows (H2).
void test_records_play_one_per_tick_and_the_end_waits_for_motor()
{
    Rig r(twoRecords());
    r.runner.follow(&r.src, 1);
    r.motor.level = true;

    TEST_ASSERT_EQUAL_UINT32(0, r.runner.segment());
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Data, r.runner.last().kind);
    TEST_ASSERT_EQUAL(1, r.out.bauds.size());
    TEST_ASSERT_EQUAL_UINT32(600, r.out.bauds[0]);
    TEST_ASSERT_EQUAL_UINT32(1, r.runner.segment());
    TEST_ASSERT_EQUAL_STRING("gap", r.runner.phase());
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Data, r.runner.last().kind);
    TEST_ASSERT_EQUAL_UINT32(750, r.out.bauds.back());
    TEST_ASSERT_FALSE(r.runner.atEnd());
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::End, r.runner.last().kind);
    TEST_ASSERT_TRUE(r.runner.atEnd());
    TEST_ASSERT_FALSE(r.runner.enabled());
    TEST_ASSERT_EQUAL(5, r.out.sent.size());
    TEST_ASSERT_EQUAL_UINT(3 + 2, r.pause.ms);

    TEST_ASSERT_FALSE(r.runner.tick());  // MOTOR still on: waits
    r.motor.level = false;
    TEST_ASSERT_FALSE(r.runner.tick());
    r.motor.level = true;
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Data, r.runner.last().kind);
    TEST_ASSERT_FALSE(r.runner.atEnd());
    TEST_ASSERT_EQUAL(8, r.out.sent.size());
    TEST_ASSERT_EQUAL_UINT8(1, r.out.sent[5]);
}

// MOTOR off between records: the tape keeps its place; MOTOR on again re-enables at 600 baud.
void test_motor_off_between_records_keeps_the_place()
{
    Rig r(twoRecords());
    r.runner.follow(&r.src, 1);
    r.motor.level = true;
    TEST_ASSERT_TRUE(r.runner.tick());
    const uint32_t place = r.runner.offset();
    r.motor.level = false;
    TEST_ASSERT_FALSE(r.runner.tick());
    TEST_ASSERT_EQUAL_UINT32(place, r.runner.offset());
    r.motor.level = true;
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL(3, r.out.bauds.size());  // 600, 600 again, then the tape's 750
    TEST_ASSERT_EQUAL_UINT32(600, r.out.bauds[1]);
    TEST_ASSERT_EQUAL_UINT8(4, r.out.sent[3]);
}

// A new generation (mount, eject, rewind) starts the tape over; the same one changes nothing.
void test_new_generation_starts_over()
{
    Rig r(twoRecords());
    TEST_ASSERT_TRUE(r.runner.follow(&r.src, 1));
    r.motor.level = true;
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_FALSE(r.runner.follow(&r.src, 1));
    TEST_ASSERT_TRUE(r.runner.offset() > 0);
    TEST_ASSERT_TRUE(r.runner.follow(&r.src, 2));
    TEST_ASSERT_EQUAL_UINT32(0, r.runner.offset());
    TEST_ASSERT_EQUAL_UINT32(0, r.runner.segment());
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL_UINT8(1, r.out.sent[3]);  // the first record again
    TEST_ASSERT_TRUE(r.runner.follow(nullptr, 3));
    TEST_ASSERT_FALSE(r.runner.tick());
}

// A busy UART: send is retried until it takes the bytes, flush waits until the line is empty, each wait 1 ms.
void test_busy_uart_is_waited_for()
{
    Rig r(twoRecords());
    r.out.busySends = 3;
    r.out.undrained = 2;
    r.runner.follow(&r.src, 1);
    r.motor.level = true;
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Data, r.runner.last().kind);
    TEST_ASSERT_EQUAL(3, r.out.sent.size());
    TEST_ASSERT_EQUAL_UINT(3, r.out.refusals);
    TEST_ASSERT_EQUAL_UINT(3 + 3 + 2, r.pause.ms);  // gap, refusals, undrained
}

// Shared mode takes at most 31 bytes at a time (EspDataIn::sharedRoom): a 600-byte record goes out in
// slices of kSliceBytes, whole and in order.
void test_a_long_record_goes_out_in_slices_the_shared_line_takes()
{
    Rig r(longRecord());
    r.out.maxSend = 31;
    r.runner.follow(&r.src, 1);
    r.motor.level = true;
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Data, r.runner.last().kind);
    TEST_ASSERT_EQUAL(601, r.out.sent.size());
    TEST_ASSERT_EQUAL_UINT(0, r.out.refusals);
    TEST_ASSERT_TRUE(r.out.largest <= FujiTapeRunner::kSliceBytes);
    for (std::size_t i = 0; i < 600; ++i) {
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(i), r.out.sent[1 + i]);
    }
}

// H3: a line that never takes a byte again stops the step within kStallMs plus the drain time; the runner
// stalls (fail closed): nothing more plays, `cas status` says so, and a new tape generation clears it.
void test_a_line_that_takes_nothing_stalls_the_runner()
{
    Rig r(twoRecords());
    r.out.refuseAll = true;
    r.runner.follow(&r.src, 1);
    r.motor.level = true;
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Replay, r.runner.last().kind);
    TEST_ASSERT_TRUE(r.runner.stalled());
    TEST_ASSERT_TRUE(r.pause.ms <= 3 + FujiTapeRunner::kStallMs + 1);
    TEST_ASSERT_TRUE(r.pause.ms >= 3 + FujiTapeRunner::kStallMs);
    TEST_ASSERT_EQUAL_STRING("line stalled", r.runner.phase());
    const unsigned refusals = r.out.refusals;
    TEST_ASSERT_FALSE(r.runner.tick());  // stalled: nothing is tried again
    TEST_ASSERT_EQUAL_UINT(refusals, r.out.refusals);

    r.out.refuseAll = false;
    TEST_ASSERT_TRUE(r.runner.follow(&r.src, 2));
    TEST_ASSERT_FALSE(r.runner.stalled());
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Data, r.runner.last().kind);
}

// H3: the rate refused for good stalls the runner too (no endless wait in enable()).
void test_a_refused_rate_stalls_the_runner()
{
    Rig r(twoRecords());
    r.out.refuseBaud = true;
    r.runner.follow(&r.src, 1);
    r.motor.level = true;
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_TRUE(r.runner.stalled());
    TEST_ASSERT_FALSE(r.runner.enabled());
    TEST_ASSERT_TRUE(r.out.sent.empty());
    TEST_ASSERT_TRUE(r.pause.ms <= FujiTapeRunner::kStallMs + 1);
}

namespace {
Watch* g_watch = nullptr;
void stopAtSend5(unsigned sends)
{
    if (sends == 5 && g_watch != nullptr) {
        g_watch->stop = true;
    }
}
}  // namespace

// H3: a command waits (StepWatch) during a long record: the step stops at the next wait, the record keeps its
// place, and once the command ran the record plays again whole from its first byte. The first record's
// replay does not fall under the beta's offset-0 rule (no MOTOR off and on needed).
void test_a_waiting_command_stops_the_step_and_the_record_replays_whole()
{
    Rig r(longRecord());
    Watch watch;
    g_watch = &watch;
    r.runner.watch(watch);
    r.out.onSend = &stopAtSend5;
    r.runner.follow(&r.src, 1);
    r.motor.level = true;
    TEST_ASSERT_TRUE(r.runner.tick());  // the first record (send 1)
    const uint32_t place = r.runner.offset();
    r.out.undrained = 1000;  // the line drains slowly: the flush waits, and checks the watch
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Replay, r.runner.last().kind);
    TEST_ASSERT_EQUAL_UINT32(place, r.runner.offset());
    TEST_ASSERT_TRUE(r.pause.ms < FujiTapeRunner::kStallMs);  // stopped by the watch, not by the stall
    TEST_ASSERT_FALSE(r.runner.stalled());

    watch.stop = false;
    r.out.onSend = nullptr;
    r.out.undrained = 0;
    r.out.sent.clear();
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Data, r.runner.last().kind);
    TEST_ASSERT_EQUAL(600, r.out.sent.size());
    TEST_ASSERT_EQUAL_UINT8(0, r.out.sent[0]);
    g_watch = nullptr;
}

namespace {
void stopAtSend1(unsigned sends)
{
    if (sends == 1 && g_watch != nullptr) {
        g_watch->stop = true;
    }
}
}  // namespace

// The first record stopped by a command: its replay is at offset 0, but that is RetroBridge's stop, not the
// tape's end, so the beta's offset-0 rule (wait for MOTOR off and on) does not apply.
void test_the_first_record_stopped_by_a_command_plays_again_without_motor_off()
{
    Rig r(twoRecords());
    Watch watch;
    g_watch = &watch;
    r.runner.watch(watch);
    r.out.onSend = &stopAtSend1;
    r.out.undrained = 1000;
    r.runner.follow(&r.src, 1);
    r.motor.level = true;
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Replay, r.runner.last().kind);
    TEST_ASSERT_EQUAL_UINT32(0, r.runner.offset());
    watch.stop = false;
    r.out.onSend = nullptr;
    r.out.undrained = 0;
    r.out.sent.clear();
    TEST_ASSERT_TRUE(r.runner.tick());  // MOTOR still on, and it plays
    TEST_ASSERT_EQUAL(FujiStepKind::Data, r.runner.last().kind);
    TEST_ASSERT_EQUAL(3, r.out.sent.size());
    g_watch = nullptr;
}

// CassetteService's two rules (fuji_service_rules.hpp): the engine is chosen whenever DATA IN is driven,
// shared or exclusive; only `status` is answered inside a step.
void test_service_rules()
{
    TEST_ASSERT_TRUE(rb::fujinet::fujiEngineChosen(true, true, true));
    TEST_ASSERT_FALSE(rb::fujinet::fujiEngineChosen(false, true, true));  // variant not built
    TEST_ASSERT_FALSE(rb::fujinet::fujiEngineChosen(true, false, true));  // DATA IN not driven (kTapeOutput)
    TEST_ASSERT_FALSE(rb::fujinet::fujiEngineChosen(true, true, false));  // no memory for the engine
    TEST_ASSERT_TRUE(rb::fujinet::answeredInsideAStep("status"));
    TEST_ASSERT_TRUE(rb::fujinet::answeredInsideAStep("  status  "));
    TEST_ASSERT_FALSE(rb::fujinet::answeredInsideAStep("status x"));
    TEST_ASSERT_FALSE(rb::fujinet::answeredInsideAStep("statusx"));
    TEST_ASSERT_FALSE(rb::fujinet::answeredInsideAStep("stat"));
    TEST_ASSERT_FALSE(rb::fujinet::answeredInsideAStep("rewind"));
    TEST_ASSERT_FALSE(rb::fujinet::answeredInsideAStep("mount status"));
    TEST_ASSERT_FALSE(rb::fujinet::answeredInsideAStep(""));
    TEST_ASSERT_FALSE(rb::fujinet::answeredInsideAStep(nullptr));
}

// H1, shared mode: the tape moves only while the cassette holds DATA IN. Take is confirmed once the line is
// free; then the records play.
void test_shared_the_tape_moves_only_with_the_line()
{
    Rig r(twoRecords());
    Lender lender;
    Line line;
    r.runner.lend(lender, line);
    r.runner.follow(&r.src, 1);
    r.motor.level = true;
    TEST_ASSERT_FALSE(r.runner.tick());  // the disk has the line
    TEST_ASSERT_TRUE(r.out.sent.empty());
    TEST_ASSERT_EQUAL_STRING("line with the disk", r.runner.phase());

    line.free = false;
    lender.request(DataInLender::Kind::Take);
    TEST_ASSERT_FALSE(r.runner.tick());
    TEST_ASSERT_FALSE(lender.answered());  // not free yet: asked again on the next turn
    line.free = true;
    TEST_ASSERT_FALSE(r.runner.tick());
    TEST_ASSERT_TRUE(lender.answered());
    TEST_ASSERT_EQUAL(DataInLender::Ack::Tape, lender.ack);
    TEST_ASSERT_TRUE(r.runner.holdsLine());

    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Data, r.runner.last().kind);
    TEST_ASSERT_EQUAL(3, r.out.sent.size());
}

namespace {

// Two data records, no baud chunk: the whole tape plays at 600. Each has a gap, so a release stops it there.
Bytes noBaudTape()
{
    Bytes f;
    chunk(f, "FUJI", 0, {});
    chunk(f, "data", 5, {7, 8});
    chunk(f, "data", 5, {9});
    return f;
}

// A data record, a baud chunk, then three more data records, each with a gap.
Bytes fourRecords()
{
    Bytes f;
    chunk(f, "FUJI", 0, {});
    chunk(f, "data", 5, {1});
    chunk(f, "baud", 750, {});
    chunk(f, "data", 5, {2});
    chunk(f, "data", 5, {3});
    chunk(f, "data", 5, {4});
    return f;
}

// The disk gives the line to the cassette: Take, answered on this tick.
void takeLine(Rig& r, Lender& lender)
{
    lender.request(DataInLender::Kind::Take);
    (void)r.runner.tick();
    TEST_ASSERT_TRUE(lender.answered());
    TEST_ASSERT_EQUAL(DataInLender::Ack::Tape, lender.ack);
}

// The disk asks the line back: the next record stops in its gap, then the line goes back.
void releaseLine(Rig& r, Lender& lender)
{
    lender.request(DataInLender::Kind::Release);
    (void)r.runner.tick();
    TEST_ASSERT_TRUE(lender.answered());
    TEST_ASSERT_EQUAL(DataInLender::Ack::Disk, lender.ack);
}

}  // namespace

// Review round 2, H4: tape A ends at 750; tape B has no baud chunk, so it plays at 600. A release and a take
// between B's records restore 600, not A's 750.
void test_shared_a_new_tape_does_not_restore_the_old_tape_rate()
{
    Bytes fileB = noBaudTape();
    MemoryTapeSource tapeB({fileB.data(), fileB.size()});
    Rig r(twoRecords());
    Lender lender;
    Line line;
    r.runner.lend(lender, line);
    r.runner.follow(&r.src, 1);
    r.motor.level = true;
    takeLine(r, lender);
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL_UINT32(750, r.out.bauds.back());  // tape A ends at 750

    TEST_ASSERT_TRUE(r.runner.follow(&tapeB, 2));
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Data, r.runner.last().kind);
    TEST_ASSERT_EQUAL_UINT32(600, r.out.bauds.back());  // enabled at 600
    TEST_ASSERT_EQUAL_UINT32(600, r.runner.baud());

    releaseLine(r, lender);
    takeLine(r, lender);
    const std::size_t before = r.out.sent.size();
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Data, r.runner.last().kind);
    TEST_ASSERT_EQUAL_UINT32(600, r.out.bauds.back());  // B's rate, not A's 750
    TEST_ASSERT_EQUAL(before + 1, r.out.sent.size());
    TEST_ASSERT_EQUAL_UINT8(9, r.out.sent.back());
}

// H4, same tape: after the baud 750 chunk, MOTOR off and on enables the cassette at 600 again. A release and a
// take then restore 600, the rate the UART had.
void test_shared_motor_off_and_on_then_a_take_restores_600()
{
    Rig r(fourRecords());
    Lender lender;
    Line line;
    r.runner.lend(lender, line);
    r.runner.follow(&r.src, 1);
    r.motor.level = true;
    takeLine(r, lender);
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL_UINT32(750, r.out.bauds.back());

    r.motor.level = false;
    TEST_ASSERT_FALSE(r.runner.tick());
    r.motor.level = true;
    TEST_ASSERT_TRUE(r.runner.tick());  // enabled at 600 again; the third record plays at 600
    TEST_ASSERT_EQUAL_UINT32(600, r.out.bauds.back());
    TEST_ASSERT_EQUAL_UINT8(3, r.out.sent.back());

    releaseLine(r, lender);
    takeLine(r, lender);
    r.out.bauds.clear();
    TEST_ASSERT_TRUE(r.runner.tick());  // the rate is restored first, then the fourth record
    TEST_ASSERT_EQUAL(1, r.out.bauds.size());
    TEST_ASSERT_EQUAL_UINT32(600, r.out.bauds[0]);
    TEST_ASSERT_EQUAL_UINT8(4, r.out.sent.back());
}

// H4, rewind (a new generation of the same tape): the tape left 750; after the rewind the first record plays
// at 600, and a release and a take restore 600.
void test_shared_a_rewind_does_not_restore_the_old_rate()
{
    Rig r(fourRecords());
    Lender lender;
    Line line;
    r.runner.lend(lender, line);
    r.runner.follow(&r.src, 1);
    r.motor.level = true;
    takeLine(r, lender);
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL_UINT32(750, r.out.bauds.back());

    TEST_ASSERT_TRUE(r.runner.follow(&r.src, 2));  // cas rewind
    const std::size_t before = r.out.sent.size();
    releaseLine(r, lender);  // the first record after the rewind is enabled at 600, then stops in its gap
    TEST_ASSERT_EQUAL_UINT32(600, r.out.bauds.back());
    TEST_ASSERT_EQUAL(before, r.out.sent.size());

    takeLine(r, lender);
    r.out.bauds.clear();
    TEST_ASSERT_TRUE(r.runner.tick());  // restored at 600, not the 750 from before the rewind
    TEST_ASSERT_EQUAL(1, r.out.bauds.size());
    TEST_ASSERT_EQUAL_UINT32(600, r.out.bauds[0]);
    TEST_ASSERT_EQUAL_UINT8(1, r.out.sent.back());  // the first record, whole
}

// H4: a refused enable does not change the rate the runner believes the line has.
void test_a_refused_enable_keeps_the_rate()
{
    Rig r(twoRecords());
    r.runner.follow(&r.src, 1);
    r.motor.level = true;
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL_UINT32(750, r.runner.baud());
    r.motor.level = false;
    TEST_ASSERT_FALSE(r.runner.tick());
    r.out.refuseBaud = true;
    r.motor.level = true;
    (void)r.runner.tick();  // the enable is refused: the runner stalls
    TEST_ASSERT_TRUE(r.runner.stalled());
    TEST_ASSERT_EQUAL_UINT32(750, r.runner.baud());
}

namespace {
Lender* g_lender = nullptr;
void releaseAtSend3(unsigned sends)
{
    if (sends == 3 && g_lender != nullptr) {
        g_lender->request(DataInLender::Kind::Release);
    }
}
}  // namespace

// H1: the disk asks the line back in the middle of a record. The step stops, the line goes back (Busy first,
// then Uart: Disk), and when the line comes back the record replays whole at the tape's rate (750), not at
// the disk's.
void test_shared_a_release_in_a_record_stops_it_and_it_replays_whole()
{
    Rig r(longRecord());
    Lender lender;
    Line line;
    g_lender = &lender;
    r.runner.lend(lender, line);
    r.runner.follow(&r.src, 1);
    r.motor.level = true;
    lender.request(DataInLender::Kind::Take);
    r.runner.tick();
    TEST_ASSERT_TRUE(r.runner.tick());  // the first record (1 byte, one send)
    const uint32_t place = r.runner.offset();
    TEST_ASSERT_EQUAL_UINT32(750, r.out.bauds.back());

    r.out.onSend = &releaseAtSend3;
    r.out.undrained = 1000;  // the flush of the first piece waits: the release is seen there
    line.back = LiveLine::GiveBack::Busy;
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Replay, r.runner.last().kind);
    TEST_ASSERT_EQUAL_UINT32(place, r.runner.offset());
    TEST_ASSERT_FALSE(r.runner.holdsLine());
    TEST_ASSERT_FALSE(lender.answered());  // Busy: asked again
    TEST_ASSERT_TRUE(r.pause.ms < 100);

    line.back = LiveLine::GiveBack::Uart;
    r.out.undrained = 0;
    TEST_ASSERT_FALSE(r.runner.tick());  // the tape does not move without the line
    TEST_ASSERT_TRUE(lender.answered());
    TEST_ASSERT_EQUAL(DataInLender::Ack::Disk, lender.ack);

    r.out.onSend = nullptr;
    r.out.bauds.clear();
    r.out.sent.clear();
    lender.request(DataInLender::Kind::Take);
    TEST_ASSERT_FALSE(r.runner.tick());
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Data, r.runner.last().kind);
    TEST_ASSERT_EQUAL(1, r.out.bauds.size());
    TEST_ASSERT_EQUAL_UINT32(750, r.out.bauds[0]);  // the tape's rate back before the record
    TEST_ASSERT_EQUAL(600, r.out.sent.size());
    TEST_ASSERT_EQUAL_UINT8(0, r.out.sent[0]);
    g_lender = nullptr;
}

// H2: the end of the tape is published, but not while the line may still carry the tape's bytes after a
// release (giveBack Busy, or Lost); Uart clears it.
void test_shared_the_end_is_published_once_the_line_is_back()
{
    Rig r(twoRecords());
    Lender lender;
    Line line;
    Sink sink;
    r.runner.lend(lender, line);
    r.runner.report(sink);
    r.runner.follow(&r.src, 1);
    r.motor.level = true;
    lender.request(DataInLender::Kind::Take);
    r.runner.tick();
    r.runner.tick();
    r.runner.tick();
    TEST_ASSERT_FALSE(sink.last());
    r.runner.tick();
    TEST_ASSERT_EQUAL(FujiStepKind::End, r.runner.last().kind);
    TEST_ASSERT_TRUE(sink.last());

    line.back = LiveLine::GiveBack::Busy;
    lender.request(DataInLender::Kind::Release);
    r.runner.tick();
    r.runner.tick();
    TEST_ASSERT_FALSE(sink.last());  // the line may still hold the tape's bytes
    line.back = LiveLine::GiveBack::Lost;
    r.runner.tick();
    TEST_ASSERT_EQUAL(DataInLender::Ack::Released, lender.ack);
    r.runner.tick();
    TEST_ASSERT_FALSE(sink.last());  // Lost: no proof the bytes left
    lender.request(DataInLender::Kind::Take);
    r.runner.tick();
    r.runner.tick();
    TEST_ASSERT_TRUE(sink.last());  // a new take: the line is the tape's again
}

// H3, shared: a release asked during a gap stops the step at once, even in the gap's last second (MOTOR's
// grace is not the line's).
void test_shared_a_release_in_the_last_second_of_a_gap_stops_at_once()
{
    Bytes f;
    chunk(f, "FUJI", 0, {});
    chunk(f, "data", 1500, {7});
    Rig r(std::move(f));
    Lender lender;
    Line line;
    r.runner.lend(lender, line);
    r.runner.follow(&r.src, 1);
    r.motor.level = true;
    lender.request(DataInLender::Kind::Take);
    r.runner.tick();
    // The release arrives once 1200 ms of the gap have gone (300 ms left).
    struct Late final : rb::fujinet::StepWatch {
        Pause* pause;
        Lender* lender;
        bool stopWanted() noexcept override
        {
            if (pause->ms == 1200 && lender->ask.kind == DataInLender::Kind::Take) {
                lender->request(DataInLender::Kind::Release);
            }
            return false;
        }
    } late;
    late.pause = &r.pause;
    late.lender = &lender;
    r.runner.watch(late);
    TEST_ASSERT_TRUE(r.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Replay, r.runner.last().kind);
    TEST_ASSERT_TRUE(r.pause.ms <= 1201);
    TEST_ASSERT_TRUE(r.out.sent.empty());
    TEST_ASSERT_TRUE(lender.answered());
    TEST_ASSERT_EQUAL(DataInLender::Ack::Disk, lender.ack);
}

// The owner's engine asks stopNow() (its motor_dropped callback): MOTOR off, a release, or a command.
void test_stop_now_answers_motor_release_and_commands()
{
    Rig r(twoRecords());
    Lender lender;
    Line line;
    Watch watch;
    r.runner.lend(lender, line);
    r.runner.watch(watch);
    r.motor.level = true;
    lender.request(DataInLender::Kind::Take);
    r.runner.tick();
    TEST_ASSERT_FALSE(r.runner.stopNow());
    r.motor.level = false;
    TEST_ASSERT_TRUE(r.runner.stopNow());
    r.motor.level = true;
    watch.stop = true;
    TEST_ASSERT_TRUE(r.runner.stopNow());
    watch.stop = false;
    lender.request(DataInLender::Kind::Release);
    TEST_ASSERT_TRUE(r.runner.stopNow());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_nothing_plays_without_tape_or_motor);
    RUN_TEST(test_records_play_one_per_tick_and_the_end_waits_for_motor);
    RUN_TEST(test_motor_off_between_records_keeps_the_place);
    RUN_TEST(test_new_generation_starts_over);
    RUN_TEST(test_busy_uart_is_waited_for);
    RUN_TEST(test_a_long_record_goes_out_in_slices_the_shared_line_takes);
    RUN_TEST(test_a_line_that_takes_nothing_stalls_the_runner);
    RUN_TEST(test_a_refused_rate_stalls_the_runner);
    RUN_TEST(test_a_waiting_command_stops_the_step_and_the_record_replays_whole);
    RUN_TEST(test_the_first_record_stopped_by_a_command_plays_again_without_motor_off);
    RUN_TEST(test_service_rules);
    RUN_TEST(test_shared_the_tape_moves_only_with_the_line);
    RUN_TEST(test_shared_a_release_in_a_record_stops_it_and_it_replays_whole);
    RUN_TEST(test_shared_the_end_is_published_once_the_line_is_back);
    RUN_TEST(test_shared_a_release_in_the_last_second_of_a_gap_stops_at_once);
    RUN_TEST(test_stop_now_answers_motor_release_and_commands);
    RUN_TEST(test_shared_a_new_tape_does_not_restore_the_old_tape_rate);
    RUN_TEST(test_shared_motor_off_and_on_then_a_take_restores_600);
    RUN_TEST(test_shared_a_rewind_does_not_restore_the_old_rate);
    RUN_TEST(test_a_refused_enable_keeps_the_rate);
    return UNITY_END();
}
