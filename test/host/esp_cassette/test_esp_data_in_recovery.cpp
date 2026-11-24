// menu-cinta T9a.2: the Fault recovery end to end on the host. The real DataInArbiter and
// DataInHandover (SIO task) against the real EspDataIn in shared mode (cassette task), on fake_idf with
// its TX model and GPIO matrix. The cassette tick here is the test's own, as §2.4 steps 3 and 6 say;
// the firmware's comes with T9a.3 and T9a.7. What it proves: Disk is confirmed only after uart_set_pin
// worked and the matrix reads U2TXD with the output on; otherwise Released, no resume(), a retry every
// 100 ms; old or late confirmations change nothing; with 512 bytes owed the release lands in time.

#include <cstdint>
#include <new>
#include <vector>

#include "driver/uart.h"
#include "fakes/idf/fake_idf.hpp"
#include "rb/board/board_profile.hpp"
#include "rb/board/fujinet_atari_v1x.hpp"
#include "rb/esp/esp_data_in.hpp"
#include "rb/menu/data_in_arbiter.hpp"
#include "rb/menu/data_in_handover.hpp"
#include "unity.h"

using rb::esp::EspDataIn;
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

constexpr int64_t kTickUs = 2000;

rb::board::CassetteOutputAssignment granted()
{
    rb::board::BoardProfile p = rb::board::fujinet_atari_v1x_profile();
    p.sioGateApproved = true;
    p.motorGateApproved = true;
    return rb::board::validate_for_cassette_output(p).value();
}

class FakeDisk final : public rb::host::DiskLine {
public:
    bool park() noexcept override
    {
        serving = false;
        return true;
    }
    LineResult resume() noexcept override
    {
        ++resumes;
        serving = true;
        return LineResult::Done;
    }
    bool serving = true;
    int resumes = 0;
};

alignas(EspDataIn) unsigned char g_storage[sizeof(EspDataIn)];

struct Rig {
    FakeDisk disk;
    DataInHandover handover;
    HandoverTapeLine tape{handover};
    DataInArbiter arbiter{disk, tape};
    EspDataIn* dataIn = nullptr;
    uint32_t answered = 0;   // the cassette answers each request once
    bool playing = false;    // the player sends records (only while the cassette has the line)
    int owed = 0;            // record bytes the player still has to send
    int want = 512;
    std::vector<uint8_t> bytes = std::vector<uint8_t>(512, 0x55);
    int released = 0;        // Released confirmations the cassette gave

    Rig()
    {
        fake_idf::reset();
        auto& s = fake_idf::state();
        s.txModel = true;
        s.uartBaud = 19200;
        (void)uart_driver_install(UART_NUM_2, 1024, 2048, 0, nullptr, 0);
        (void)uart_set_pin(UART_NUM_2, 21, 33, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        s.uartInstalls = 0;  // the disk's install at boot; from here on nobody installs or deletes
        s.uartDeletes = 0;
        dataIn = new (g_storage) EspDataIn();
        TEST_ASSERT_TRUE(dataIn->attachShared(granted(), 2048));
    }

    // The cassette task's tick, as plan §2.4 steps 3 and 6 describe it.
    void cassetteTick()
    {
        if (playing && owed > 0) {
            const int n = want < owed ? want : owed;
            if (dataIn->send(rb::core::Span<const uint8_t>(bytes.data(), static_cast<std::size_t>(n)))) {
                owed -= n;
                want = 512;
            } else {
                want = 1;
            }
        }
        const DataInHandover::Pending p = handover.pending();
        if (p.seq == 0 || p.seq == answered) {
            return;
        }
        if (p.kind == HandoverRequest::Take) {
            if (dataIn->hardwareFree() && dataIn->setBaud(600)) {
                handover.confirm(p.seq, HandoverAck::Tape);
                answered = p.seq;
                playing = true;
            }
            return;
        }
        playing = false;  // the player discards from here on
        switch (dataIn->giveBack()) {
        case EspDataIn::GiveBack::Uart:
            handover.confirm(p.seq, HandoverAck::Disk);
            answered = p.seq;
            break;
        case EspDataIn::GiveBack::Lost:
            handover.confirm(p.seq, HandoverAck::Released);
            answered = p.seq;
            ++released;
            break;
        case EspDataIn::GiveBack::Busy:
            break;
        }
    }

    DataInChange step(const DataInInputs& in)
    {
        cassetteTick();
        fake_idf::tx_advance(kTickUs);
        const DataInChange change = arbiter.step(in, static_cast<uint64_t>(fake_idf::state().nowUs));
        TEST_ASSERT_FALSE_MESSAGE(disk.serving && playing, "two transmitters on GPIO21");
        return change;
    }

    void stepUntil(const DataInInputs& in, DataInChange change, int limit = 2000)
    {
        for (int i = 0; i < limit; ++i) {
            const DataInChange got = step(in);
            if (got == change) {
                return;
            }
            TEST_ASSERT_NOT_EQUAL_MESSAGE(DataInChange::ReleaseTimedOut, got, "the release expired");
        }
        TEST_FAIL_MESSAGE("the expected change never came");
    }

    void steps(const DataInInputs& in, int n)
    {
        for (int i = 0; i < n; ++i) {
            (void)step(in);
        }
    }
};

DataInInputs playingTape()
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

void setUp(void) {}
void tearDown(void) {}

// The normal path: take, play records, give back; the disk resumes once, the release lands in time.
void test_take_and_give_back_with_512_bytes_owed()
{
    Rig r;
    r.stepUntil(playingTape(), DataInChange::ToTape);
    r.owed = 512;
    r.steps(playingTape(), 300);
    TEST_ASSERT_GREATER_THAN_INT(0, r.owed);
    r.stepUntil(wantsDisk(), DataInChange::ToDisk);
    TEST_ASSERT_EQUAL_INT(1, r.disk.resumes);
    TEST_ASSERT_EQUAL_UINT(0, fake_idf::state().uartInstalls);
    TEST_ASSERT_EQUAL_UINT(0, fake_idf::state().uartDeletes);
}

// uart_set_pin fails: Released, no resume(), asked again every 100 ms; works on a later try: one resume().
void test_a_failed_uart_set_pin_is_released_and_retried_until_it_works()
{
    Rig r;
    r.stepUntil(playingTape(), DataInChange::ToTape);
    fake_idf::state().failUartSetPin = true;
    r.stepUntil(wantsDisk(), DataInChange::Released);
    TEST_ASSERT_EQUAL(DataInOwner::Released, r.arbiter.owner());
    TEST_ASSERT_EQUAL_INT(0, r.disk.resumes);
    TEST_ASSERT_FALSE(r.disk.serving);
    r.steps(wantsDisk(), 150);  // 300 ms: retried every 100 ms, still failing
    TEST_ASSERT_GREATER_OR_EQUAL_INT(3, r.released);
    TEST_ASSERT_EQUAL_INT(0, r.disk.resumes);
    fake_idf::state().failUartSetPin = false;
    r.stepUntil(wantsDisk(), DataInChange::ToDisk);
    TEST_ASSERT_EQUAL_INT(1, r.disk.resumes);
    r.steps(wantsDisk(), 100);
    TEST_ASSERT_EQUAL_INT(1, r.disk.resumes);
    TEST_ASSERT_EQUAL(DataInOwner::Disk, r.arbiter.owner());
}

// uart_set_pin says OK but the matrix reads another signal: the same as a failure.
void test_a_matrix_reading_otherwise_is_released()
{
    Rig r;
    r.stepUntil(playingTape(), DataInChange::ToTape);
    auto& s = fake_idf::state();
    s.routeForced = true;
    s.forcedSel = fake_idf::kRmtOut0Idx;
    s.forcedEnabled = true;
    r.stepUntil(wantsDisk(), DataInChange::Released);
    TEST_ASSERT_EQUAL_INT(0, r.disk.resumes);
    s.routeForced = false;
    r.stepUntil(wantsDisk(), DataInChange::ToDisk);
    TEST_ASSERT_EQUAL_INT(1, r.disk.resumes);
}

// A Disk confirmation for an old sequence changes nothing.
void test_an_old_confirmation_is_ignored()
{
    Rig r;
    r.stepUntil(playingTape(), DataInChange::ToTape);
    const uint32_t take = r.answered;
    fake_idf::state().failUartSetPin = true;
    r.stepUntil(wantsDisk(), DataInChange::Released);
    r.handover.confirm(take, HandoverAck::Disk);  // the take's sequence, long gone
    r.steps(wantsDisk(), 10);
    TEST_ASSERT_EQUAL_INT(0, r.disk.resumes);
    TEST_ASSERT_NOT_EQUAL(DataInOwner::Disk, r.arbiter.owner());
}

// The RMT keeps the pin past kReleaseTimeoutUs: Fault, no resume(); a Disk that arrives late is ignored.
void test_a_late_confirmation_is_ignored()
{
    Rig r;
    r.stepUntil(playingTape(), DataInChange::ToTape);
    TEST_ASSERT_TRUE(r.dataIn->push(rb::cassette::FskPortion{false, 200}));
    TEST_ASSERT_TRUE(r.dataIn->push(rb::cassette::FskPortion{true, 200}));
    r.dataIn->pump();
    r.dataIn->pump();
    TEST_ASSERT_TRUE(r.dataIn->fskActive());
    fake_idf::state().failRmtDisable = true;  // the RMT does not let go: giveBack() stays Busy
    DataInChange got = DataInChange::None;
    uint32_t expired = 0;
    for (int i = 0; i < 2000 && got != DataInChange::ReleaseTimedOut; ++i) {
        expired = r.handover.pending().seq;
        got = r.step(wantsDisk());
    }
    TEST_ASSERT_EQUAL(DataInChange::ReleaseTimedOut, got);
    TEST_ASSERT_EQUAL(DataInOwner::Fault, r.arbiter.owner());
    r.handover.confirm(expired, HandoverAck::Disk);  // after its deadline
    (void)r.arbiter.step(wantsDisk(), static_cast<uint64_t>(fake_idf::state().nowUs));
    TEST_ASSERT_EQUAL_INT(0, r.disk.resumes);
    TEST_ASSERT_EQUAL(DataInOwner::Fault, r.arbiter.owner());
    fake_idf::state().failRmtDisable = false;  // the RMT lets go: the next retry gives the line back
    r.stepUntil(wantsDisk(), DataInChange::ToDisk);
    TEST_ASSERT_EQUAL_INT(1, r.disk.resumes);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_take_and_give_back_with_512_bytes_owed);
    RUN_TEST(test_a_failed_uart_set_pin_is_released_and_retried_until_it_works);
    RUN_TEST(test_a_matrix_reading_otherwise_is_released);
    RUN_TEST(test_an_old_confirmation_is_ignored);
    RUN_TEST(test_a_late_confirmation_is_ignored);
    return UNITY_END();
}
