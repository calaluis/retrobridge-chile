// DataInArbiter (spec menu-cinta, plan §2.3 and §2.4): who owns the DATA IN line, the disk (SIO,
// EspSioUart) or the tape (EspDataIn). Both drive UART2 TX on GPIO21, so never both at once: the
// disk parks before the tape is asked, and resumes only after the tape gave the line back. The lines
// here are fakes that answer at once and write every call to one shared log, so the order and the
// rules are checked; the asynchronous answers (T9a.1) are in test_data_in_handover.

#include <string>
#include <vector>

#include "rb/menu/data_in_arbiter.hpp"
#include "unity.h"

using rb::host::DataInArbiter;
using rb::host::DataInChange;
using rb::host::DataInInputs;
using rb::host::DataInOwner;

namespace {

std::vector<std::string> g_calls;

class FakeDisk final : public rb::host::DiskLine {
public:
    bool park() noexcept override
    {
        g_calls.push_back("disk.park");
        if (busyParks > 0) {  // the disk's TX is not empty yet
            --busyParks;
            return false;
        }
        active = false;
        return true;
    }
    rb::host::LineResult resume() noexcept override
    {
        g_calls.push_back("disk.resume");
        if (failResumes > 0) {
            --failResumes;
            return rb::host::LineResult::Failed;
        }
        active = true;
        return rb::host::LineResult::Done;
    }
    int busyParks = 0;
    int failResumes = 0;
    bool active = true;  // the SIO line serves frames before the arbiter starts
};

class FakeTape final : public rb::host::TapeLine {
public:
    rb::host::LineResult take() noexcept override
    {
        g_calls.push_back("tape.take");
        if (failTake) {
            return rb::host::LineResult::Failed;
        }
        driving = true;
        return rb::host::LineResult::Done;
    }
    rb::host::LineResult release() noexcept override
    {
        g_calls.push_back("tape.release");
        if (failReleases > 0) {
            --failReleases;
            return rb::host::LineResult::Failed;
        }
        driving = false;
        return rb::host::LineResult::Done;
    }
    void abandon() noexcept override { g_calls.push_back("tape.abandon"); }
    bool failTake = false;
    int failReleases = 0;
    bool driving = false;
};

struct Rig {
    FakeDisk disk;
    FakeTape tape;
    DataInArbiter arbiter{disk, tape};
    uint64_t now = 0;

    // One step of the SIO loop, 10 ms after the last. Never both transmitters.
    DataInChange step(const DataInInputs& in)
    {
        now += 10000;
        const DataInChange change = arbiter.step(in, now);
        TEST_ASSERT_FALSE_MESSAGE(disk.active && tape.driving, "two transmitters on GPIO21");
        return change;
    }
};

DataInInputs gated()
{
    DataInInputs in;
    in.gates = true;
    return in;
}

DataInInputs playing()
{
    DataInInputs in = gated();
    in.motorOn = true;
    in.tapeReady = true;
    return in;
}

void assertCalls(std::initializer_list<const char*> expected)
{
    TEST_ASSERT_EQUAL_size_t_MESSAGE(expected.size(), g_calls.size(), "number of line calls");
    std::size_t i = 0;
    for (const char* call : expected) {
        TEST_ASSERT_EQUAL_STRING(call, g_calls[i++].c_str());
    }
}

}  // namespace

void setUp(void) { g_calls.clear(); }
void tearDown(void) {}

// The disk owns the line from the start, and nothing is called while nothing asks for the tape.
void test_the_disk_owns_the_line_at_start()
{
    Rig rig;
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Disk);
    TEST_ASSERT_TRUE(rig.step(gated()) == DataInChange::None);
    assertCalls({});
}

// Rule 1: MOTOR on, a tape ready and both gates: the disk parks, then the tape takes the line.
void test_motor_on_with_a_ready_tape_hands_the_line_to_the_tape_in_order()
{
    Rig rig;
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::ToTape);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Tape);
    assertCalls({"disk.park", "tape.take"});
}

// Rule 4 (AC8): without M3a/H4 the line never leaves the disk, whatever MOTOR does.
void test_without_the_gates_the_disk_keeps_the_line()
{
    Rig rig;
    DataInInputs in = playing();
    in.gates = false;
    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT_TRUE(rig.step(in) == DataInChange::None);
    }
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Disk);
    assertCalls({});
}

// No tape ready, or MOTOR off: the disk keeps the line.
void test_motor_without_a_ready_tape_or_a_tape_without_motor_keeps_the_disk()
{
    Rig rig;
    DataInInputs in = gated();
    in.motorOn = true;
    rig.step(in);
    in.motorOn = false;
    in.tapeReady = true;
    rig.step(in);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Disk);
    assertCalls({});
}

// Rule 1, review of PR #85: MOTOR already on when the tape becomes ready (the remembered tape is
// read again at a START power-up and the key comes first): the line goes in that step, no new edge.
void test_motor_already_on_hands_the_line_when_the_tape_becomes_ready()
{
    Rig rig;
    DataInInputs in = gated();
    in.motorOn = true;
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_TRUE(rig.step(in) == DataInChange::None);
    }
    in.tapeReady = true;  // MOTOR stays on: no edge
    TEST_ASSERT_TRUE(rig.step(in) == DataInChange::ToTape);
    assertCalls({"disk.park", "tape.take"});
}

// Rule 2: MOTOR pauses between blocks; the tape keeps the line.
void test_the_tape_keeps_the_line_while_motor_pauses()
{
    Rig rig;
    rig.step(playing());
    DataInInputs pause = playing();
    pause.motorOn = false;
    for (int i = 0; i < 10; ++i) {
        TEST_ASSERT_TRUE(rig.step(pause) == DataInChange::None);
    }
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::None);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Tape);
    assertCalls({"disk.park", "tape.take"});
}

// Rule 3: COMMAND low with MOTOR off (the Atari wants the disk): release, then resume.
void test_command_with_motor_off_gives_the_line_back_to_the_disk()
{
    Rig rig;
    rig.step(playing());
    DataInInputs in = playing();
    in.motorOn = false;
    in.commandLow = true;
    TEST_ASSERT_TRUE(rig.step(in) == DataInChange::ToDisk);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Disk);
    assertCalls({"disk.park", "tape.take", "tape.release", "disk.resume"});
}

// COMMAND low while MOTOR is still on: the tape is playing; nothing changes.
void test_command_with_motor_on_keeps_the_tape()
{
    Rig rig;
    rig.step(playing());
    DataInInputs in = playing();
    in.commandLow = true;
    TEST_ASSERT_TRUE(rig.step(in) == DataInChange::None);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Tape);
}

// Rule 3: the end of the tape with MOTOR off, a power-off, or an eject/rewind give the line back.
void test_end_of_tape_power_off_or_eject_give_the_line_back()
{
    for (int reason = 0; reason < 3; ++reason) {
        g_calls.clear();
        Rig rig;
        rig.step(playing());
        DataInInputs in = playing();
        if (reason == 0) {
            in.tapeEnded = true;
            TEST_ASSERT_TRUE(rig.step(in) == DataInChange::None);  // MOTOR still on: not yet
            in.motorOn = false;
        } else if (reason == 1) {
            in.poweredOff = true;  // even with MOTOR seen on
        } else {
            in.tapeStopped = true;
        }
        TEST_ASSERT_TRUE(rig.step(in) == DataInChange::ToDisk);
        TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Disk);
        assertCalls({"disk.park", "tape.take", "tape.release", "disk.resume"});
    }
}

// After the tape gave the line back, a new MOTOR on with a ready tape takes it again.
void test_the_tape_can_take_the_line_again()
{
    Rig rig;
    rig.step(playing());
    DataInInputs off = gated();
    off.poweredOff = true;
    rig.step(off);
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::ToTape);
    assertCalls({"disk.park", "tape.take", "tape.release", "disk.resume", "disk.park", "tape.take"});
}

// Rule 5: take() is refused: the disk resumes at once; the next try waits 100 ms.
void test_a_failed_take_gives_the_line_back_to_the_disk()
{
    Rig rig;
    rig.tape.failTake = true;
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::TakeFailed);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Disk);
    assertCalls({"disk.park", "tape.take", "disk.resume"});
}

// Plan §2.4, «Reanudar el disco»: resume() fails after the tape gave the line back. DiskFault: D1
// stays silent (no ToDisk), resume() is retried every 100 ms, and MOTOR cannot take the tape meanwhile.
void test_a_failed_resume_is_a_disk_fault_retried_every_100_ms()
{
    Rig rig;
    rig.step(playing());
    rig.disk.failResumes = 2;
    DataInInputs off = gated();
    off.poweredOff = true;
    TEST_ASSERT_TRUE(rig.step(off) == DataInChange::ResumeFailed);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::DiskFault);
    TEST_ASSERT_FALSE(rig.disk.active);
    DataInInputs idle = gated();
    for (int i = 0; i < 9; ++i) {  // 90 ms: not yet
        TEST_ASSERT_TRUE(rig.step(idle) == DataInChange::None);
    }
    TEST_ASSERT_TRUE(rig.step(idle) == DataInChange::None);       // 100 ms: the second try fails, same state
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::None);  // DiskFault: MOTOR never asks for the tape
    for (int i = 0; i < 8; ++i) {
        rig.step(idle);
    }
    TEST_ASSERT_TRUE(rig.step(idle) == DataInChange::ToDisk);  // 200 ms: the third try works
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Disk);
    assertCalls({"disk.park", "tape.take", "tape.release", "disk.resume", "disk.resume", "disk.resume"});
}

// release() is refused: the tape may still drive GPIO21, so the disk stays parked (Fault); the release
// is asked again every 100 ms, and the disk resumes only after it is done.
void test_a_refused_release_is_a_fault_and_never_resumes_the_disk_until_done()
{
    Rig rig;
    rig.step(playing());
    rig.tape.failReleases = 1;
    DataInInputs off = gated();
    off.poweredOff = true;
    TEST_ASSERT_TRUE(rig.step(off) == DataInChange::ReleaseFailed);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Fault);
    TEST_ASSERT_FALSE(rig.disk.active);
    for (int i = 0; i < 9; ++i) {
        TEST_ASSERT_TRUE(rig.step(gated()) == DataInChange::None);
    }
    TEST_ASSERT_TRUE(rig.step(gated()) == DataInChange::ToDisk);
    assertCalls({"disk.park", "tape.take", "tape.release", "tape.release", "disk.resume"});
}

// Plan §2.4 step 1: the disk parks only with its TX empty. A busy park is tried again next step,
// and the tape is not asked before the disk has parked.
void test_a_busy_disk_parks_on_a_later_step_before_the_tape_is_asked()
{
    Rig rig;
    rig.disk.busyParks = 2;
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::None);
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::None);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Disk);
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::ToTape);
    assertCalls({"disk.park", "disk.park", "disk.park", "tape.take"});
}

// A gate withdrawn while the tape plays (a package without them, or a fault): back to the disk.
void test_losing_the_gates_gives_the_line_back()
{
    Rig rig;
    rig.step(playing());
    DataInInputs in = playing();
    in.gates = false;
    TEST_ASSERT_TRUE(rig.step(in) == DataInChange::ToDisk);
    assertCalls({"disk.park", "tape.take", "tape.release", "disk.resume"});
}

// Review of PR #85 (T4): take() keeps failing while MOTOR stays on with the tape ready. The disk
// keeps UART2 while it waits: one attempt per 100 ms, never one per step (10 per 10 steps before).
void test_a_take_that_keeps_failing_is_retried_every_100_ms_with_the_disk_up()
{
    Rig rig;
    rig.tape.failTake = true;
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::TakeFailed);
    for (int i = 0; i < 9; ++i) {  // 90 ms: the disk stays active, nothing is called
        TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::None);
        TEST_ASSERT_TRUE(rig.disk.active);
    }
    assertCalls({"disk.park", "tape.take", "disk.resume"});
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::TakeFailed);  // 100 ms: the second try
    rig.tape.failTake = false;
    for (int i = 0; i < 9; ++i) {
        TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::None);
    }
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::ToTape);  // 100 ms later: it works
    assertCalls({"disk.park", "tape.take", "disk.resume", "disk.park", "tape.take", "disk.resume",
                 "disk.park", "tape.take"});
}

// ---- T7: the readings of the SIO task become the arbiter's inputs ----

using rb::host::DataInReadings;
using rb::host::dataInInputs;

DataInReadings fullGates()
{
    DataInReadings r;
    r.gatesOpen = true;
    r.tapeOutput = true;
    return r;
}

// Every reading reaches its input: COMMAND low = asserted, a deck with a tape = ready, not stopped.
void test_the_readings_become_the_inputs()
{
    DataInReadings r = fullGates();
    r.motorOn = true;
    r.commandHigh = false;
    r.tapeOnC = true;
    r.tapeEnded = true;
    r.atariOff = true;
    const DataInInputs in = dataInInputs(r);
    TEST_ASSERT_TRUE(in.gates);
    TEST_ASSERT_TRUE(in.motorOn);
    TEST_ASSERT_TRUE(in.commandLow);
    TEST_ASSERT_TRUE(in.tapeReady);
    TEST_ASSERT_TRUE(in.tapeEnded);
    TEST_ASSERT_TRUE(in.poweredOff);
    TEST_ASSERT_FALSE(in.tapeStopped);
}

// COMMAND high (idle) is not asserted; a deck with no tape is stopped and not ready (eject).
void test_command_idle_and_an_empty_deck()
{
    DataInReadings r = fullGates();
    r.commandHigh = true;
    r.tapeOnC = false;
    const DataInInputs in = dataInInputs(r);
    TEST_ASSERT_FALSE(in.commandLow);
    TEST_ASSERT_FALSE(in.tapeReady);
    TEST_ASSERT_TRUE(in.tapeStopped);
}

// AC8, rule 4: either gate missing, or a cassette that cannot drive DATA IN in this build (until
// T9): no gates. Run a whole load with MOTOR on and a tape ready: the SIO line is never touched.
void test_without_gates_or_tape_output_the_line_never_moves()
{
    for (int missing = 0; missing < 2; ++missing) {
        g_calls.clear();
        DataInReadings r = fullGates();
        if (missing == 0) {
            r.gatesOpen = false;
        } else {
            r.tapeOutput = false;
        }
        r.tapeOnC = true;
        Rig rig;
        for (int i = 0; i < 50; ++i) {
            r.motorOn = (i % 10) < 7;          // MOTOR on and off, as between blocks
            r.commandHigh = (i % 10) != 9;     // and the Atari talking to a disk now and then
            TEST_ASSERT_TRUE(rig.step(dataInInputs(r)) == DataInChange::None);
        }
        TEST_ASSERT_FALSE(dataInInputs(r).gates);
        TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Disk);
        assertCalls({});
    }
}

// Both gates and a cassette that can drive DATA IN (T9): the arbiter may hand the line over.
void test_with_gates_and_tape_output_the_tape_can_take_the_line()
{
    DataInReadings r = fullGates();
    r.motorOn = true;
    r.tapeOnC = true;
    Rig rig;
    TEST_ASSERT_TRUE(rig.step(dataInInputs(r)) == DataInChange::ToTape);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_disk_owns_the_line_at_start);
    RUN_TEST(test_motor_on_with_a_ready_tape_hands_the_line_to_the_tape_in_order);
    RUN_TEST(test_without_the_gates_the_disk_keeps_the_line);
    RUN_TEST(test_motor_without_a_ready_tape_or_a_tape_without_motor_keeps_the_disk);
    RUN_TEST(test_motor_already_on_hands_the_line_when_the_tape_becomes_ready);
    RUN_TEST(test_the_tape_keeps_the_line_while_motor_pauses);
    RUN_TEST(test_command_with_motor_off_gives_the_line_back_to_the_disk);
    RUN_TEST(test_command_with_motor_on_keeps_the_tape);
    RUN_TEST(test_end_of_tape_power_off_or_eject_give_the_line_back);
    RUN_TEST(test_the_tape_can_take_the_line_again);
    RUN_TEST(test_a_failed_take_gives_the_line_back_to_the_disk);
    RUN_TEST(test_a_take_that_keeps_failing_is_retried_every_100_ms_with_the_disk_up);
    RUN_TEST(test_a_failed_resume_is_a_disk_fault_retried_every_100_ms);
    RUN_TEST(test_a_refused_release_is_a_fault_and_never_resumes_the_disk_until_done);
    RUN_TEST(test_a_busy_disk_parks_on_a_later_step_before_the_tape_is_asked);
    RUN_TEST(test_losing_the_gates_gives_the_line_back);
    RUN_TEST(test_the_readings_become_the_inputs);
    RUN_TEST(test_command_idle_and_an_empty_deck);
    RUN_TEST(test_without_gates_or_tape_output_the_line_never_moves);
    RUN_TEST(test_with_gates_and_tape_output_the_tape_can_take_the_line);
    return UNITY_END();
}
