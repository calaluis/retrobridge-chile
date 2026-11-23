// DataInArbiter (spec menu-cinta, plan §2.3, T4): who owns the DATA IN line, the disk (SIO,
// EspSioUart) or the tape (EspDataIn). Both install UART2 on GPIO21, so they are never installed
// at once: the disk ends before the tape takes, the tape releases before the disk begins. The
// lines here are fakes that write every call to one shared log, so the order is checked.

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
    bool begin() noexcept override
    {
        g_calls.push_back("disk.begin");
        if (failBegins > 0) {
            --failBegins;
            return false;
        }
        installed = true;
        return true;
    }
    void end() noexcept override
    {
        g_calls.push_back("disk.end");
        installed = false;
    }
    int failBegins = 0;
    bool installed = true;  // the SIO line is up before the arbiter starts
};

class FakeTape final : public rb::host::TapeLine {
public:
    bool take() noexcept override
    {
        g_calls.push_back("tape.take");
        if (failTake) {
            return false;
        }
        installed = true;
        return true;
    }
    bool release() noexcept override
    {
        g_calls.push_back("tape.release");
        if (failReleases > 0) {
            --failReleases;
            return false;
        }
        installed = false;
        return true;
    }
    bool failTake = false;
    int failReleases = 0;
    bool installed = false;
};

struct Rig {
    FakeDisk disk;
    FakeTape tape;
    DataInArbiter arbiter{disk, tape};
    uint64_t now = 0;

    // One step of the SIO loop, 10 ms after the last. Never both lines installed.
    DataInChange step(const DataInInputs& in)
    {
        now += 10000;
        const DataInChange change = arbiter.step(in, now);
        TEST_ASSERT_FALSE_MESSAGE(disk.installed && tape.installed, "UART2 installed twice");
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

// Rule 1: MOTOR on, a tape ready and both gates: the disk ends, then the tape takes the line.
void test_motor_on_with_a_ready_tape_hands_the_line_to_the_tape_in_order()
{
    Rig rig;
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::ToTape);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Tape);
    assertCalls({"disk.end", "tape.take"});
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
    assertCalls({"disk.end", "tape.take"});
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
    assertCalls({"disk.end", "tape.take"});
}

// Rule 3: COMMAND low with MOTOR off (the Atari wants the disk): release, then begin.
void test_command_with_motor_off_gives_the_line_back_to_the_disk()
{
    Rig rig;
    rig.step(playing());
    DataInInputs in = playing();
    in.motorOn = false;
    in.commandLow = true;
    TEST_ASSERT_TRUE(rig.step(in) == DataInChange::ToDisk);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Disk);
    assertCalls({"disk.end", "tape.take", "tape.release", "disk.begin"});
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
        assertCalls({"disk.end", "tape.take", "tape.release", "disk.begin"});
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
    assertCalls({"disk.end", "tape.take", "tape.release", "disk.begin", "disk.end", "tape.take"});
}

// Rule 5: take() fails: the disk begins again at once; the next step may try again.
void test_a_failed_take_gives_the_line_back_to_the_disk()
{
    Rig rig;
    rig.tape.failTake = true;
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::TakeFailed);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Disk);
    assertCalls({"disk.end", "tape.take", "disk.begin"});
}

// Rule 5: begin() fails: no owner, retried every 100 ms until the disk is back; never the tape.
void test_a_failed_begin_is_retried_every_100_ms()
{
    Rig rig;
    rig.step(playing());
    rig.disk.failBegins = 2;
    DataInInputs off = gated();
    off.poweredOff = true;
    TEST_ASSERT_TRUE(rig.step(off) == DataInChange::BeginFailed);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::None);
    DataInInputs idle = gated();
    for (int i = 0; i < 9; ++i) {  // 90 ms: not yet
        TEST_ASSERT_TRUE(rig.step(idle) == DataInChange::None);
    }
    TEST_ASSERT_TRUE(rig.step(idle) == DataInChange::BeginFailed);  // 100 ms: the second try fails
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::None);   // no owner: the tape cannot take it
    for (int i = 0; i < 9; ++i) {
        rig.step(idle);
    }
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Disk);
    assertCalls({"disk.end", "tape.take", "tape.release", "disk.begin", "disk.begin", "disk.begin"});
}

// release() fails: the tape may still hold UART2, so the disk does not begin; release is retried
// every 100 ms, and the disk begins only after it succeeds.
void test_a_failed_release_never_begins_the_disk_until_it_succeeds()
{
    Rig rig;
    rig.step(playing());
    rig.tape.failReleases = 1;
    DataInInputs off = gated();
    off.poweredOff = true;
    TEST_ASSERT_TRUE(rig.step(off) == DataInChange::ReleaseFailed);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Tape);
    for (int i = 0; i < 9; ++i) {
        TEST_ASSERT_TRUE(rig.step(gated()) == DataInChange::None);
    }
    TEST_ASSERT_TRUE(rig.step(gated()) == DataInChange::ToDisk);
    assertCalls({"disk.end", "tape.take", "tape.release", "tape.release", "disk.begin"});
}

// A gate withdrawn while the tape plays (a package without them, or a fault): back to the disk.
void test_losing_the_gates_gives_the_line_back()
{
    Rig rig;
    rig.step(playing());
    DataInInputs in = playing();
    in.gates = false;
    TEST_ASSERT_TRUE(rig.step(in) == DataInChange::ToDisk);
    assertCalls({"disk.end", "tape.take", "tape.release", "disk.begin"});
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
    RUN_TEST(test_a_failed_begin_is_retried_every_100_ms);
    RUN_TEST(test_a_failed_release_never_begins_the_disk_until_it_succeeds);
    RUN_TEST(test_losing_the_gates_gives_the_line_back);
    return UNITY_END();
}
