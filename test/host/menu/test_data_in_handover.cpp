// DataInHandover and the asynchronous DataInArbiter (spec menu-cinta, plan §2.4, T9a.1). The SIO task
// steps the arbiter; a fake cassette task answers the handover on its own tick, late, never, with the
// wrong sequence, or refusing. Every step checks that GPIO21 never has two transmitters and that the
// disk serves frames only while the arbiter says Disk. The SIO path is also checked to allocate nothing.

#include <cstdint>
#include <cstdlib>
#include <new>

#include "rb/menu/data_in_arbiter.hpp"
#include "rb/menu/data_in_handover.hpp"
#include "rb/menu/tape_progress.hpp"
#include "unity.h"

// --- Allocations on the SIO path -------------------------------------------------

namespace {
bool g_counting = false;
unsigned long g_allocations = 0;

void* counted(std::size_t size) noexcept
{
    if (g_counting) {
        ++g_allocations;
    }
    return std::malloc(size == 0 ? 1 : size);
}
}  // namespace

// Built with -fno-exceptions: allocation failure aborts instead of throwing.
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

using rb::host::DataInArbiter;
using rb::host::DataInChange;
using rb::host::DataInHandover;
using rb::host::DataInInputs;
using rb::host::DataInOwner;
using rb::host::HandoverAck;
using rb::host::HandoverRequest;
using rb::host::HandoverTapeLine;
using rb::host::LineResult;

namespace {

constexpr uint64_t kTickUs = 2000;  // the cassette task's tick; the SIO loop steps at the same pace here

class FakeDisk final : public rb::host::DiskLine {
public:
    bool park() noexcept override
    {
        ++parks;
        serving = false;
        return true;
    }
    LineResult resume() noexcept override
    {
        ++resumes;
        if (failResumes > 0) {
            --failResumes;
            return LineResult::Failed;
        }
        serving = true;
        return LineResult::Done;
    }
    bool serving = true;
    int parks = 0;
    int resumes = 0;
    int failResumes = 0;
};

// How the fake cassette answers one kind of request.
enum class Answer { Confirm, Never, WrongSeq, Refuse, ReleaseOnly };

// The cassette task: reads the latest request on each tick and answers it after `delayTicks` ticks.
struct FakeCassette {
    DataInHandover& handover;
    Answer onTake = Answer::Confirm;
    Answer onRelease = Answer::Confirm;
    int takeDelay = 1;
    int releaseDelay = 1;
    bool driving = false;  // the cassette drives GPIO21 (UART2 at 600 baud or the RMT)

    uint32_t seenSeq = 0;
    int waited = 0;
    bool answered = true;  // the boot request (Release, 0) is already answered Disk

    void tick()
    {
        const DataInHandover::Pending p = handover.pending();
        if (p.seq != seenSeq) {
            seenSeq = p.seq;
            waited = 0;
            answered = false;
        }
        if (answered) {
            return;
        }
        ++waited;
        const bool take = p.kind == HandoverRequest::Take;
        if (waited < (take ? takeDelay : releaseDelay)) {
            return;
        }
        answered = true;
        switch (take ? onTake : onRelease) {
        case Answer::Never:
            answered = false;  // keeps looking, never answers
            return;
        case Answer::WrongSeq:
            handover.confirm(p.seq + 7, take ? HandoverAck::Tape : HandoverAck::Disk);
            answered = false;
            return;
        case Answer::Refuse:
            handover.confirm(p.seq, HandoverAck::Refused);
            return;
        case Answer::ReleaseOnly:  // GPIO21 could not be given back to UART2: the line is let go
            driving = false;
            handover.confirm(p.seq, HandoverAck::Released);
            return;
        case Answer::Confirm:
        default:
            driving = take;
            handover.confirm(p.seq, take ? HandoverAck::Tape : HandoverAck::Disk);
            return;
        }
    }
};

struct Rig {
    FakeDisk disk;
    DataInHandover handover;
    HandoverTapeLine tape{handover};
    DataInArbiter arbiter{disk, tape};
    FakeCassette cassette{handover};
    uint64_t now = 0;
    int changes = 0;

    // One tick: the cassette task, then the SIO task (counted: no allocation allowed there).
    DataInChange step(const DataInInputs& in)
    {
        now += kTickUs;
        cassette.tick();
        g_counting = true;
        const DataInChange change = arbiter.step(in, now);
        g_counting = false;
        check();
        if (change != DataInChange::None) {
            ++changes;
        }
        return change;
    }

    void check()
    {
        TEST_ASSERT_FALSE_MESSAGE(disk.serving && cassette.driving, "two transmitters on GPIO21");
        TEST_ASSERT_EQUAL_MESSAGE(arbiter.owner() == DataInOwner::Disk, disk.serving,
                                  "the disk serves frames only while the arbiter says Disk");
    }

    // The 65XE with MOTOR on sends no COMMAND frame: each SIO step waits its whole poll (20 ms,
    // SioSession::kPollTimeoutUs) plus the step's own work, while the cassette ticks every 2 ms.
    DataInChange stepAfterIdlePoll(const DataInInputs& in, uint64_t sioPeriodUs)
    {
        const uint64_t until = now + sioPeriodUs;
        while (now + kTickUs <= until) {
            now += kTickUs;
            cassette.tick();
        }
        now = until;
        g_counting = true;
        const DataInChange change = arbiter.step(in, now);
        g_counting = false;
        check();
        if (change != DataInChange::None) {
            ++changes;
        }
        return change;
    }

    // Steps until `change` is returned; fails after `limit` steps.
    void stepUntil(const DataInInputs& in, DataInChange change, int limit = 2000)
    {
        for (int i = 0; i < limit; ++i) {
            if (step(in) == change) {
                return;
            }
        }
        TEST_FAIL_MESSAGE("the expected change never came");
    }
};

DataInInputs playing()
{
    DataInInputs in;
    in.gates = true;
    in.motorOn = true;
    in.tapeReady = true;
    return in;
}

DataInInputs wantsDisk()
{
    DataInInputs in;
    in.gates = true;
    in.poweredOff = true;
    return in;
}

}  // namespace

void setUp(void) { g_allocations = 0; }
void tearDown(void) {}

// ---- DataInHandover alone ----

// A confirmation counts only for its own sequence; every request gets a new one, never 0.
void test_an_ack_counts_only_for_its_own_sequence()
{
    DataInHandover h;
    const uint32_t a = h.request(HandoverRequest::Take);
    TEST_ASSERT_NOT_EQUAL(0u, a);
    HandoverAck ack;
    TEST_ASSERT_FALSE(h.ackFor(a, ack));             // nothing yet (the boot ack is for 0)
    TEST_ASSERT_TRUE(h.pending().kind == HandoverRequest::Take);
    TEST_ASSERT_EQUAL_UINT32(a, h.pending().seq);
    h.confirm(a, HandoverAck::Tape);
    TEST_ASSERT_TRUE(h.ackFor(a, ack));
    TEST_ASSERT_TRUE(ack == HandoverAck::Tape);
    const uint32_t b = h.request(HandoverRequest::Release);
    TEST_ASSERT_NOT_EQUAL(a, b);
    TEST_ASSERT_FALSE(h.ackFor(b, ack));             // the Tape ack was for a, not b
    h.confirm(a, HandoverAck::Disk);                 // an old sequence
    TEST_ASSERT_FALSE(h.ackFor(b, ack));
    h.confirm(b, HandoverAck::Disk);
    TEST_ASSERT_TRUE(h.ackFor(b, ack));
    TEST_ASSERT_TRUE(ack == HandoverAck::Disk);
}

// The line publishes once and then polls; asking for the other kind is a new request.
void test_the_tape_line_publishes_once_and_polls()
{
    DataInHandover h;
    HandoverTapeLine line{h};
    TEST_ASSERT_TRUE(line.take() == LineResult::Pending);
    const uint32_t seq = h.pending().seq;
    TEST_ASSERT_TRUE(line.take() == LineResult::Pending);
    TEST_ASSERT_EQUAL_UINT32(seq, h.pending().seq);  // polled, not asked again
    h.confirm(seq, HandoverAck::Tape);
    TEST_ASSERT_TRUE(line.take() == LineResult::Done);
    TEST_ASSERT_TRUE(line.release() == LineResult::Pending);
    TEST_ASSERT_TRUE(h.pending().kind == HandoverRequest::Release);
    TEST_ASSERT_NOT_EQUAL(seq, h.pending().seq);
    h.confirm(h.pending().seq, HandoverAck::Released);
    TEST_ASSERT_TRUE(line.release() == LineResult::Released);
    TEST_ASSERT_TRUE(line.take() == LineResult::Pending);
    h.confirm(h.pending().seq, HandoverAck::Refused);
    TEST_ASSERT_TRUE(line.take() == LineResult::Failed);
}

// abandon(): a late ack for the abandoned request is ignored; the next call asks again.
void test_an_abandoned_request_ignores_its_late_ack()
{
    DataInHandover h;
    HandoverTapeLine line{h};
    TEST_ASSERT_TRUE(line.release() == LineResult::Pending);
    const uint32_t old = h.pending().seq;
    line.abandon();
    TEST_ASSERT_TRUE(line.release() == LineResult::Pending);  // a new request
    const uint32_t fresh = h.pending().seq;
    TEST_ASSERT_NOT_EQUAL(old, fresh);
    h.confirm(old, HandoverAck::Disk);  // late, for the abandoned one
    TEST_ASSERT_TRUE(line.release() == LineResult::Pending);
    h.confirm(fresh, HandoverAck::Disk);
    TEST_ASSERT_TRUE(line.release() == LineResult::Done);
}

// ---- The arbiter over the handover ----

// Step 2: take is Pending -> TakePending: the disk is parked and the cassette does not drive yet.
// Step 4: the cassette confirms Tape on its tick -> Tape.
void test_a_take_waits_for_the_cassette_with_no_transmitter()
{
    Rig rig;
    rig.cassette.takeDelay = 3;
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::TakeRequested);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::TakePending);
    TEST_ASSERT_FALSE(rig.disk.serving);
    TEST_ASSERT_FALSE(rig.cassette.driving);
    rig.stepUntil(playing(), DataInChange::ToTape, 5);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Tape);
    TEST_ASSERT_TRUE(rig.cassette.driving);
}

// fix-cinta-data-in-65xe (65XE, e286c35): with MOTOR on the Atari only listens, so the SIO task steps
// every ~21.5 ms (20 ms poll + work). The cassette confirmed in 2 ms, yet every take expired at 21-22 ms:
// the deadline is checked before the ack. A take confirmed in time must reach Tape on the next step.
void test_a_take_confirmed_in_time_reaches_the_tape_when_the_sio_step_waits_a_whole_poll()
{
    constexpr uint64_t kIdleSioStepUs = 20000 + 1500;  // SioSession::kPollTimeoutUs + the step's work
    Rig rig;
    rig.cassette.takeDelay = 1;  // the cassette's next tick: free, confirmed
    TEST_ASSERT_TRUE(rig.stepAfterIdlePoll(playing(), kIdleSioStepUs) == DataInChange::TakeRequested);
    TEST_ASSERT_TRUE(rig.stepAfterIdlePoll(playing(), kIdleSioStepUs) == DataInChange::ToTape);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Tape);
    TEST_ASSERT_TRUE(rig.cassette.driving);
}

// The deadline covers two whole idle SIO steps, so one late step never expires a confirmed take.
void test_the_take_deadline_covers_two_idle_sio_steps()
{
    TEST_ASSERT_TRUE(DataInArbiter::kTakeTimeoutUs > 2 * (20000 + 1500));
}

// A full load: take, MOTOR pauses, end of the tape -> ReleasePending -> Disk, resume() once.
void test_a_release_resumes_the_disk_only_after_the_cassette_confirms_disk()
{
    Rig rig;
    rig.cassette.releaseDelay = 5;
    rig.stepUntil(playing(), DataInChange::ToTape);
    DataInInputs ended = playing();
    ended.motorOn = false;
    ended.tapeEnded = true;
    TEST_ASSERT_TRUE(rig.step(ended) == DataInChange::ReleaseRequested);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::ReleasePending);
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_TRUE(rig.step(ended) == DataInChange::None);
        TEST_ASSERT_EQUAL(0, rig.disk.resumes);  // never resume() while the tape may still drive
    }
    rig.stepUntil(ended, DataInChange::ToDisk, 5);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Disk);
    TEST_ASSERT_EQUAL(1, rig.disk.resumes);
}

// A confirmation with another sequence is not ours: Done never comes; the take expires at its deadline.
void test_a_confirmation_with_another_sequence_is_ignored()
{
    Rig rig;
    rig.cassette.onTake = Answer::WrongSeq;
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::TakeRequested);
    for (uint64_t t = kTickUs; t < DataInArbiter::kTakeTimeoutUs; t += kTickUs) {
        TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::None);
        TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::TakePending);
    }
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::TakeTimedOut);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::ReleasePending);
}

// Plan §2.4: a take that expires becomes a release; the disk resumes only after the cassette
// confirms Disk. The cassette's late Tape (old sequence) arrives after the deadline: ignored, and
// GPIO21 never has two transmitters (checked on every step).
void test_an_expired_take_becomes_a_release_and_a_late_tape_ack_is_ignored()
{
    Rig rig;
    rig.cassette.takeDelay = static_cast<int>(DataInArbiter::kTakeTimeoutUs / kTickUs) + 5;  // after the deadline
    rig.cassette.releaseDelay = 2;
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::TakeRequested);
    rig.stepUntil(playing(), DataInChange::TakeTimedOut, 40);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::ReleasePending);
    TEST_ASSERT_EQUAL(0, rig.disk.resumes);
    // The cassette now sees the release (a new sequence) and answers it; the old take is never answered.
    rig.stepUntil(playing(), DataInChange::ToDisk, 10);
    TEST_ASSERT_EQUAL(1, rig.disk.resumes);
    TEST_ASSERT_FALSE(rig.cassette.driving);
    // The failed take waits 100 ms before MOTOR may ask again (rule 5).
    const uint64_t failedAt = rig.now;
    while (rig.step(playing()) != DataInChange::TakeRequested) {
    }
    TEST_ASSERT_TRUE(rig.now - failedAt >= DataInArbiter::kRetryUs - 4 * kTickUs);
}

// The cassette confirms Tape just after the take expired, before it sees the release: the SIO task
// ignores the late Tape, the cassette drives briefly, the disk stays parked until Disk is confirmed.
void test_a_tape_ack_racing_the_deadline_never_resumes_the_disk_under_the_tape()
{
    Rig rig;
    rig.cassette.takeDelay = static_cast<int>(DataInArbiter::kTakeTimeoutUs / kTickUs);  // exactly at the deadline
    rig.cassette.releaseDelay = 3;
    rig.step(playing());
    rig.stepUntil(playing(), DataInChange::TakeTimedOut, 40);
    rig.stepUntil(playing(), DataInChange::ToDisk, 40);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Disk);
    TEST_ASSERT_FALSE(rig.cassette.driving);
}

// A release that expires (600 ms) is a Fault: no resume(); asked again every 100 ms; a recovery that
// finally works resumes the disk once.
void test_an_expired_release_is_a_fault_retried_every_100_ms()
{
    Rig rig;
    rig.stepUntil(playing(), DataInChange::ToTape);
    rig.cassette.onRelease = Answer::Never;
    TEST_ASSERT_TRUE(rig.step(wantsDisk()) == DataInChange::ReleaseRequested);
    const uint64_t asked = rig.now;
    rig.stepUntil(wantsDisk(), DataInChange::ReleaseTimedOut, 400);
    TEST_ASSERT_TRUE(rig.now - asked >= DataInArbiter::kReleaseTimeoutUs);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Fault);
    const uint32_t expiredSeq = rig.handover.pending().seq;
    // The retry asks again with a new sequence after 100 ms, and Fault stays quiet (no change) meanwhile.
    int steps = 0;
    while (rig.handover.pending().seq == expiredSeq) {
        TEST_ASSERT_TRUE(rig.step(wantsDisk()) == DataInChange::None);
        ++steps;
    }
    TEST_ASSERT_TRUE(static_cast<uint64_t>(steps) * kTickUs >= DataInArbiter::kRetryUs);
    TEST_ASSERT_EQUAL(0, rig.disk.resumes);
    // A late Disk ack for the expired sequence changes nothing.
    rig.handover.confirm(expiredSeq, HandoverAck::Disk);
    TEST_ASSERT_TRUE(rig.step(wantsDisk()) == DataInChange::None);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Fault);
    // The cassette recovers: the retry is confirmed and the disk resumes, once.
    rig.cassette.onRelease = Answer::Confirm;
    rig.cassette.answered = false;
    rig.stepUntil(wantsDisk(), DataInChange::ToDisk, 400);
    TEST_ASSERT_EQUAL(1, rig.disk.resumes);
}

// Released: the cassette could not give GPIO21 back to UART2 and let the line go. Not Disk: the disk
// stays parked; the recovery is asked every 100 ms; when it works, one resume().
void test_released_never_resumes_the_disk_until_a_verified_disk_ack()
{
    Rig rig;
    rig.stepUntil(playing(), DataInChange::ToTape);
    rig.cassette.onRelease = Answer::ReleaseOnly;
    rig.step(wantsDisk());
    rig.stepUntil(wantsDisk(), DataInChange::Released, 5);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Released);
    TEST_ASSERT_FALSE(rig.cassette.driving);
    for (int i = 0; i < 200; ++i) {  // 400 ms of failed recoveries: same state, no event, no resume
        TEST_ASSERT_TRUE(rig.step(wantsDisk()) == DataInChange::None);
    }
    TEST_ASSERT_EQUAL(0, rig.disk.resumes);
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::None);  // MOTOR cannot take a released line
    rig.cassette.onRelease = Answer::Confirm;
    rig.stepUntil(wantsDisk(), DataInChange::ToDisk, 100);
    TEST_ASSERT_EQUAL(1, rig.disk.resumes);
}

// Fault then Released then Fault: each change of state is one event, never one per retry.
void test_fault_and_released_report_one_event_per_change()
{
    Rig rig;
    rig.stepUntil(playing(), DataInChange::ToTape);
    rig.cassette.onRelease = Answer::Never;
    rig.step(wantsDisk());
    rig.stepUntil(wantsDisk(), DataInChange::ReleaseTimedOut, 400);
    rig.changes = 0;
    rig.cassette.onRelease = Answer::ReleaseOnly;
    rig.stepUntil(wantsDisk(), DataInChange::Released, 100);
    for (int i = 0; i < 300; ++i) {
        rig.step(wantsDisk());
    }
    TEST_ASSERT_EQUAL(1, rig.changes);
}

// A refused take (Failed, not expired) is rule 5: the disk resumes at once.
void test_a_refused_take_resumes_the_disk_at_once()
{
    Rig rig;
    rig.cassette.onTake = Answer::Refuse;
    rig.step(playing());
    rig.stepUntil(playing(), DataInChange::TakeFailed, 5);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Disk);
    TEST_ASSERT_EQUAL(1, rig.disk.resumes);
    TEST_ASSERT_FALSE(rig.cassette.driving);
}

// Plan §2.4, «Reanudar el disco»: the cassette confirms Disk but resume() fails. DiskFault: no ToDisk,
// D1 silent; resume() again every 100 ms; the first that works is one ToDisk.
void test_a_disk_ack_with_a_failed_resume_is_a_disk_fault()
{
    Rig rig;
    rig.stepUntil(playing(), DataInChange::ToTape);
    rig.disk.failResumes = 2;
    rig.step(wantsDisk());
    rig.stepUntil(wantsDisk(), DataInChange::ResumeFailed, 5);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::DiskFault);
    TEST_ASSERT_FALSE(rig.disk.serving);
    rig.changes = 0;
    rig.stepUntil(wantsDisk(), DataInChange::ToDisk, 200);
    TEST_ASSERT_EQUAL(1, rig.changes);
    TEST_ASSERT_EQUAL(3, rig.disk.resumes);
}

// MOTOR stops before the cassette confirmed the take: the take becomes a release at once.
void test_giving_back_during_a_pending_take_releases()
{
    Rig rig;
    rig.cassette.takeDelay = 4;
    TEST_ASSERT_TRUE(rig.step(playing()) == DataInChange::TakeRequested);
    TEST_ASSERT_TRUE(rig.step(wantsDisk()) == DataInChange::ReleaseRequested);
    rig.stepUntil(wantsDisk(), DataInChange::ToDisk, 10);
    TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Disk);
}

// R10: the SIO path allocates nothing, through every state: take, tape, release, faults, recovery.
void test_the_sio_path_allocates_nothing()
{
    Rig rig;
    rig.stepUntil(playing(), DataInChange::ToTape);
    rig.step(wantsDisk());
    rig.stepUntil(wantsDisk(), DataInChange::ToDisk);
    rig.cassette.onTake = Answer::Never;
    rig.cassette.onRelease = Answer::Never;
    rig.stepUntil(playing(), DataInChange::TakeTimedOut);
    rig.stepUntil(wantsDisk(), DataInChange::ReleaseTimedOut);
    rig.cassette.onRelease = Answer::ReleaseOnly;
    rig.stepUntil(wantsDisk(), DataInChange::Released);
    rig.disk.failResumes = 1;
    rig.cassette.onRelease = Answer::Confirm;
    rig.stepUntil(wantsDisk(), DataInChange::ResumeFailed);
    rig.stepUntil(wantsDisk(), DataInChange::ToDisk);
    TEST_ASSERT_EQUAL_UINT32(0, g_allocations);
}

// ---- T9a.4: rule 3 with TapeProgress ----

// The cassette publishes "ended" only with its output empty. MOTOR off at the end of the tape: while the
// last bytes are still going out the arbiter keeps the tape (no release asked); once they are out it asks
// for the disk at once.
void test_rule_3_waits_for_the_published_end()
{
    Rig rig;
    rig.cassette.releaseDelay = 2;
    rig.stepUntil(playing(), DataInChange::ToTape);
    rb::host::TapeProgress progress;
    bool atEnd = true;
    bool drained = false;  // the last record's bytes still in UART2
    DataInInputs in = playing();
    in.motorOn = false;
    for (int i = 0; i < 50; ++i) {
        progress.publish(atEnd && drained);  // cassette task
        in.tapeEnded = progress.ended();     // SIO task
        TEST_ASSERT_TRUE(rig.step(in) == DataInChange::None);
        TEST_ASSERT_TRUE(rig.arbiter.owner() == DataInOwner::Tape);
    }
    drained = true;
    progress.publish(atEnd && drained);
    in.tapeEnded = progress.ended();
    TEST_ASSERT_TRUE(rig.step(in) == DataInChange::ReleaseRequested);
    rig.stepUntil(in, DataInChange::ToDisk, 10);
    TEST_ASSERT_EQUAL(1, rig.disk.resumes);
}

// Rewound or ejected after the end: the cassette publishes false again and MOTOR off alone does not
// hand the line back (a pause between two parts keeps the tape).
void test_a_published_end_goes_back_to_false()
{
    rb::host::TapeProgress progress;
    TEST_ASSERT_FALSE(progress.ended());
    progress.publish(true);
    TEST_ASSERT_TRUE(progress.ended());
    progress.publish(false);
    TEST_ASSERT_FALSE(progress.ended());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_an_ack_counts_only_for_its_own_sequence);
    RUN_TEST(test_the_tape_line_publishes_once_and_polls);
    RUN_TEST(test_an_abandoned_request_ignores_its_late_ack);
    RUN_TEST(test_a_take_waits_for_the_cassette_with_no_transmitter);
    RUN_TEST(test_a_take_confirmed_in_time_reaches_the_tape_when_the_sio_step_waits_a_whole_poll);
    RUN_TEST(test_the_take_deadline_covers_two_idle_sio_steps);
    RUN_TEST(test_a_release_resumes_the_disk_only_after_the_cassette_confirms_disk);
    RUN_TEST(test_a_confirmation_with_another_sequence_is_ignored);
    RUN_TEST(test_an_expired_take_becomes_a_release_and_a_late_tape_ack_is_ignored);
    RUN_TEST(test_a_tape_ack_racing_the_deadline_never_resumes_the_disk_under_the_tape);
    RUN_TEST(test_an_expired_release_is_a_fault_retried_every_100_ms);
    RUN_TEST(test_released_never_resumes_the_disk_until_a_verified_disk_ack);
    RUN_TEST(test_fault_and_released_report_one_event_per_change);
    RUN_TEST(test_a_refused_take_resumes_the_disk_at_once);
    RUN_TEST(test_a_disk_ack_with_a_failed_resume_is_a_disk_fault);
    RUN_TEST(test_giving_back_during_a_pending_take_releases);
    RUN_TEST(test_the_sio_path_allocates_nothing);
    RUN_TEST(test_rule_3_waits_for_the_published_end);
    RUN_TEST(test_a_published_end_goes_back_to_false);
    return UNITY_END();
}
