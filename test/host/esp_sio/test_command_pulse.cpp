// Issue #125, H2 (review of afa7c03, matrix asked in the review of d20616b): a COMMAND pulse that starts and
// ends inside the SIO step, before the arbiter samples COMMAND. Models main/menu_sio.cpp's order per loop
// pass: first the SIO step (the COMMAND window), then readings.commandHigh = line.commandHigh() and
// g_arbiter.step(). Real EspSioUart (fake ESP-IDF time, 1 tick = 1 ms), real SioLineTransport,
// UartDiskLine, DataInArbiter and, where said, a real SioSession with a SioCommandDispatcher over a
// DriveBay with an ATR on D1. The tape side is a stub. It does NOT run sioTask(), the menu transport
// (MenuSioTransport, device $5A), the drive memory, the power watch or the cassette task.
//
// Expectation (independent of any fix): rule 3 of the arbiter, as specified in DataInArbiter::givesBack():
// with MOTOR off, a command from the Atari (COMMAND asserted) gives DATA IN back to the disk. The tests that
// fail are the H2 RED; none is weakened to pass. Results are printed as "H2|..." lines for the FTD table.

#include <cstdint>
#include <cstdio>

#include "fakes/fake_byte_source.hpp"
#include "fake_idf.hpp"
#include "rb/app/drive_bay.hpp"
#include "rb/app/image_location.hpp"
#include "rb/app/inline_drive_mailbox.hpp"
#include "rb/app/usecases/apply_drive_request.hpp"
#include "rb/domain/disk_image.hpp"
#include "rb/domain/drive_number.hpp"
#include "rb/esp/esp_sio_uart.hpp"
#include "rb/menu/data_in_arbiter.hpp"
#include "rb/sio/sio_checksum.hpp"
#include "rb/sio/sio_command_dispatcher.hpp"
#include "rb/sio/sio_line_transport.hpp"
#include "rb/sio/sio_session.hpp"
#include "sio_disk_line.hpp"
#include "support/atr_header.hpp"
#include "unity.h"

using rb::esp::EspSioUart;
using rb::host::DataInArbiter;
using rb::host::DataInOwner;
using rb::host::DataInReadings;
using rb::host::LineResult;
using rb::sio::StepOutcome;

namespace {

constexpr EspSioUart::Pins kPins{39, 33, 21};
constexpr int64_t kMs = 1000;

class IdfClock final : public rb::app::Clock {
public:
    uint64_t nowMicros() const override { return static_cast<uint64_t>(fake_idf::state().nowUs); }
    void waitUntil(uint64_t micros) override
    {
        auto& s = fake_idf::state();
        if (static_cast<int64_t>(micros) > s.nowUs) {
            s.nowUs = static_cast<int64_t>(micros);
        }
    }
};

class StubTape final : public rb::host::TapeLine {
public:
    LineResult take() noexcept override { return LineResult::Done; }
    LineResult release() noexcept override
    {
        ++releases;
        return releaseResult;
    }
    void abandon() noexcept override {}
    LineResult releaseResult = LineResult::Done;
    int releases = 0;
};

// A status command to D1, as the Atari sends it: t0 = 900 us after the fall, one byte per 521 us
// (19200 baud), COMMAND released t1 = 800 us after the last byte. Returns the release time.
int64_t scheduleStatusFrame(int64_t fallUs)
{
    auto& s = fake_idf::state();
    uint8_t frame[5] = {0x31, 0x53, 0x00, 0x00, 0x00};
    frame[4] = rb::sio::sio_checksum(rb::core::Span<const uint8_t>{frame, 4});
    int64_t at = fallUs + 900;
    for (uint8_t b : frame) {
        at += 521;
        s.rxAt.emplace_back(at, b);
    }
    s.commandLowFromUs = fallUs;
    s.commandLowUntilUs = at + 800;
    return at + 800;
}

struct Rig {
    EspSioUart line{kPins};
    rb::menusio::UartDiskLine disk;
    StubTape tape;
    DataInArbiter arbiter{disk, tape};
    IdfClock clock;
    rb::sio::SioLineTransport transport{line, clock};
    DataInReadings readings;
    // The real SIO step, for the cases that say so.
    rb::test::FakeByteSource source{rb::test::make_atr_header(rb::test::sd_data_size(720), 128),
                                    rb::test::kAtrHeaderSize + rb::test::sd_data_size(720)};
    rb::app::DriveBay bay{8};
    rb::app::InlineDriveMailbox mailbox;
    rb::app::ApplyDriveRequest apply{bay, mailbox};
    rb::sio::SioCommandDispatcher dispatcher{bay};
    rb::sio::SioSession session{transport, clock, dispatcher, apply};
    StepOutcome lastStep = StepOutcome::Idle;

    Rig()
    {
        TEST_ASSERT_TRUE(line.begin());
        disk.attach(line);
        readings.gatesOpen = true;
        readings.tapeOutput = true;
        readings.tapeOnC = true;
        TEST_ASSERT_TRUE(bay.mount(rb::domain::DriveNumber::from(1).value(),
                                   rb::domain::DiskImage::open(source).value(),
                                   rb::app::ImageLocation::from("d1.atr").value())
                             .ok());
    }

    void arbiterStep()
    {
        readings.commandHigh = line.commandHigh();
        (void)arbiter.step(rb::host::dataInInputs(readings), static_cast<uint64_t>(fake_idf::state().nowUs));
    }

    // One pass of the SIO loop, in main/menu_sio.cpp's order. withSession: the real SioSession::step().
    void pass(bool withSession = false)
    {
        if (withSession) {
            lastStep = session.step();
        } else {
            (void)transport.waitCommandFrame(rb::sio::SioSession::kPollTimeoutUs);
        }
        arbiterStep();
    }

    // The tape owns DATA IN, the UART is parked, then the game stops MOTOR (issue #125's state).
    void tapeOwnsWithMotorOff()
    {
        readings.motorOn = true;
        readings.commandHigh = true;
        (void)arbiter.step(rb::host::dataInInputs(readings), static_cast<uint64_t>(fake_idf::state().nowUs));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(DataInOwner::Tape), static_cast<int>(arbiter.owner()));
        TEST_ASSERT_EQUAL_UINT32(1, line.writeFailures().parkedNow.load());
        readings.motorOn = false;
    }

    bool gaveBack() const { return arbiter.owner() != DataInOwner::Tape; }
};

void pulseFromNow(int64_t offsetUs, int64_t lengthUs)
{
    auto& s = fake_idf::state();
    s.commandLowFromUs = s.nowUs + offsetUs;
    s.commandLowUntilUs = s.commandLowFromUs + lengthUs;
}

// One pulse, one pass. Phases: offset of the fall from the start of the window. A negative offset is a
// fall during the previous arbiter step (COMMAND already low when the window opens).
constexpr int64_t kPhases[] = {-500, 0, 250, 500, 750, 10 * kMs, 19 * kMs + 500};
constexpr int kPhaseCount = sizeof(kPhases) / sizeof(kPhases[0]);

int missesFor(int64_t lengthUs)
{
    int misses = 0;
    std::printf("H2|duracion %lld ms|", static_cast<long long>(lengthUs / kMs));
    for (int64_t phase : kPhases) {
        fake_idf::reset();
        fake_idf::state().commandPin = 39;
        fake_idf::state().nowUs = 1000 * kMs;
        Rig rig;
        rig.tapeOwnsWithMotorOff();
        pulseFromNow(phase, lengthUs);
        rig.pass();
        if (!rig.gaveBack()) {
            ++misses;
            // Never unparked while the tape keeps the line (R1): nothing in this model asks for it.
            TEST_ASSERT_EQUAL_UINT32(1, rig.line.writeFailures().parkedNow.load());
        }
        const rb::sio::SioLineStats st = rig.transport.stats();
        std::printf("fase %lld us: %s (cortas %u, pegado %u, tramas %u, rx_err %u)|", static_cast<long long>(phase),
                    rig.gaveBack() ? "devuelve" : "PIERDE", static_cast<unsigned>(st.shortFrames),
                    static_cast<unsigned>(st.stuckLows), static_cast<unsigned>(st.frames),
                    static_cast<unsigned>(st.receiveErrors));
    }
    std::printf("perdidos %d de %d\n", misses, kPhaseCount);
    return misses;
}

}  // namespace

void setUp()
{
    fake_idf::reset();
    fake_idf::state().commandPin = 39;
}
void tearDown() {}

// --- 1. Duration x phase. Expected for every row: no pulse is missed (0 of 7).
void test_pulse_1ms_is_not_missed() { TEST_ASSERT_EQUAL_INT(0, missesFor(1 * kMs)); }
void test_pulse_2ms_is_not_missed() { TEST_ASSERT_EQUAL_INT(0, missesFor(2 * kMs)); }
void test_pulse_3ms_is_not_missed() { TEST_ASSERT_EQUAL_INT(0, missesFor(3 * kMs)); }
void test_pulse_4ms_is_not_missed() { TEST_ASSERT_EQUAL_INT(0, missesFor(4 * kMs)); }
void test_pulse_6ms_is_not_missed() { TEST_ASSERT_EQUAL_INT(0, missesFor(6 * kMs)); }
void test_pulse_8ms_is_not_missed() { TEST_ASSERT_EQUAL_INT(0, missesFor(8 * kMs)); }
void test_pulse_10ms_is_not_missed() { TEST_ASSERT_EQUAL_INT(0, missesFor(10 * kMs)); }
void test_pulse_20ms_is_not_missed() { TEST_ASSERT_EQUAL_INT(0, missesFor(20 * kMs)); }

// --- 2. Retries at several intervals. A 3 ms pulse every `interval` ms, 10 of them, scheduled in fake time
// whatever the loop is doing. Expected: DATA IN given back by the end. No retry count of the real Atari is
// assumed; 10 is only a bound for the test.
// Review of bf04542 (H1): the 10 pulses are a fixed schedule, written before the run (fall k at
// first + k * interval, 3 ms each), never moved. The fake line shows the scheduled pulse that is current or
// next; a wait that spans a whole pulse means nobody looked during it, it is not rescheduled. Counted
// apart: pulses emitted (their whole interval elapsed in the run), windows that saw a fall (short frames
// plus stuck lows), and short frames.
namespace {
constexpr int kPulses = 10;
constexpr int64_t kPulseUs = 3 * kMs;
int64_t g_falls[kPulses] = {};
void showScheduledPulse()
{
    auto& s = fake_idf::state();
    for (int64_t fall : g_falls) {
        if (fall + kPulseUs > s.nowUs) {  // current or next
            s.commandLowFromUs = fall;
            s.commandLowUntilUs = fall + kPulseUs;
            return;
        }
    }
    s.commandLowFromUs = -1;
    s.commandLowUntilUs = -1;
}
bool retriesGiveBack(int64_t intervalUs)
{
    fake_idf::reset();
    auto& s = fake_idf::state();
    s.commandPin = 39;
    s.nowUs = 1000 * kMs;
    Rig rig;
    rig.tapeOwnsWithMotorOff();
    const int64_t first = s.nowUs + 1 * kMs;
    for (int k = 0; k < kPulses; ++k) {
        g_falls[k] = first + k * intervalUs;
    }
    for (int k = 1; k < kPulses; ++k) {
        TEST_ASSERT_EQUAL_INT64(intervalUs, g_falls[k] - g_falls[k - 1]);  // the cadence asked
    }
    s.onDelay = showScheduledPulse;
    const int64_t lastEnd = g_falls[kPulses - 1] + kPulseUs;
    while (s.nowUs < lastEnd + 25 * kMs && !rig.gaveBack()) {
        showScheduledPulse();
        rig.pass();
    }
    s.onDelay = nullptr;
    int emitted = 0;
    for (int64_t fall : g_falls) {
        emitted += fall + kPulseUs <= s.nowUs ? 1 : 0;
    }
    const rb::sio::SioLineStats st = rig.transport.stats();
    const unsigned seen = static_cast<unsigned>(st.shortFrames + st.stuckLows);
    std::printf("H2|reintentos cada %lld ms|%s|emitidos %d, vistos por la ventana %u, cortas %u, pegado %u\n",
                static_cast<long long>(intervalUs / kMs), rig.gaveBack() ? "devuelve" : "PIERDE",
                emitted, seen, static_cast<unsigned>(st.shortFrames), static_cast<unsigned>(st.stuckLows));
    if (!rig.gaveBack()) {
        TEST_ASSERT_EQUAL_INT(kPulses, emitted);  // a miss only counts once all 10 were on the line
    }
    return rig.gaveBack();
}
}  // namespace

void test_retries_every_5ms_give_back() { TEST_ASSERT_TRUE(retriesGiveBack(5 * kMs)); }
void test_retries_every_10ms_give_back() { TEST_ASSERT_TRUE(retriesGiveBack(10 * kMs)); }
void test_retries_every_20ms_give_back() { TEST_ASSERT_TRUE(retriesGiveBack(20 * kMs)); }
void test_retries_every_50ms_give_back() { TEST_ASSERT_TRUE(retriesGiveBack(50 * kMs)); }

// --- 3. How the window ends: released (short frame), still low (stuck), receive error.
void test_released_pulse_is_a_short_frame_and_still_low_is_stuck()
{
    {
        Rig rig;
        rig.tapeOwnsWithMotorOff();
        pulseFromNow(1 * kMs, 3 * kMs);
        rig.pass();
        TEST_ASSERT_EQUAL_UINT32(1, rig.transport.stats().shortFrames);
        TEST_ASSERT_EQUAL_UINT32(0, rig.transport.stats().stuckLows);
    }
    fake_idf::reset();
    fake_idf::state().commandPin = 39;
    {
        Rig rig;
        rig.tapeOwnsWithMotorOff();
        pulseFromNow(1 * kMs, 1000 * kMs);
        rig.pass();
        TEST_ASSERT_EQUAL_UINT32(0, rig.transport.stats().shortFrames);
        TEST_ASSERT_EQUAL_UINT32(1, rig.transport.stats().stuckLows);
    }
}

// A receive error needs read() to fail; a parked read never fails (it waits and returns 0 bytes), so with
// the tape owning DATA IN this path is out of reach. Asserted, so a change to read() shows up here.
void test_a_parked_read_never_ends_as_a_receive_error()
{
    Rig rig;
    rig.tapeOwnsWithMotorOff();
    pulseFromNow(1 * kMs, 3 * kMs);
    rig.pass();
    TEST_ASSERT_EQUAL_UINT32(0, rig.transport.stats().receiveErrors);
    TEST_ASSERT_EQUAL_UINT32(1, rig.line.writeFailures().readsWhileParked.load());
}

// --- 4. The other give-back paths, without any pulse (controls GREEN).
void test_tape_ended_with_motor_off_gives_back()
{
    Rig rig;
    rig.tapeOwnsWithMotorOff();
    rig.readings.tapeEnded = true;
    rig.pass();
    TEST_ASSERT_TRUE(rig.gaveBack());
}
void test_gates_closed_give_back()
{
    Rig rig;
    rig.tapeOwnsWithMotorOff();
    rig.readings.gatesOpen = false;
    rig.pass();
    TEST_ASSERT_TRUE(rig.gaveBack());
}
void test_tape_ejected_gives_back()
{
    Rig rig;
    rig.tapeOwnsWithMotorOff();
    rig.readings.tapeOnC = false;
    rig.pass();
    TEST_ASSERT_TRUE(rig.gaveBack());
}
void test_power_off_seen_gives_back()
{
    Rig rig;
    rig.tapeOwnsWithMotorOff();
    rig.readings.atariOff = true;
    rig.pass();
    TEST_ASSERT_TRUE(rig.gaveBack());
}
// Negative control: MOTOR off, no command, nothing else: the tape keeps the line (rule 2 pause).
void test_no_command_and_nothing_else_keeps_the_tape()
{
    Rig rig;
    rig.tapeOwnsWithMotorOff();
    for (int i = 0; i < 5; ++i) {
        rig.pass();
    }
    TEST_ASSERT_FALSE(rig.gaveBack());
}
// Negative control: MOTOR on, a 3 ms pulse: the tape keeps the line (rule 2).
void test_a_3ms_pulse_with_motor_on_keeps_the_tape()
{
    Rig rig;
    rig.tapeOwnsWithMotorOff();
    rig.readings.motorOn = true;
    pulseFromNow(1 * kMs, 3 * kMs);
    rig.pass();
    TEST_ASSERT_FALSE(rig.gaveBack());
}

// --- 5. A real command frame (5 bytes) through the real SioSession::step().
// Expected: DATA IN given back. While parked, no byte is read and nothing is transmitted.
void test_a_status_frame_through_the_session_with_motor_off_gives_back()
{
    Rig rig;
    rig.tapeOwnsWithMotorOff();
    auto& s = fake_idf::state();
    (void)scheduleStatusFrame(s.nowUs + 1 * kMs);
    rig.pass(true);
    std::printf("H2|trama de estado D1 (5 bytes) por SioSession::step()|paso %d|%s|tx %u bytes\n",
                static_cast<int>(rig.lastStep), rig.gaveBack() ? "devuelve" : "PIERDE",
                static_cast<unsigned>(s.written.size()));
    // Review of bf04542 (M1): how this frame ended. It was never read, so it can never be answered later;
    // only a command after a safe give-back can be (test below).
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepOutcome::Ignored), static_cast<int>(rig.lastStep));
    TEST_ASSERT_EQUAL_UINT32(1, rig.transport.stats().shortFrames);
    TEST_ASSERT_EQUAL_UINT32(1, rig.line.writeFailures().readsWhileParked.load());
    TEST_ASSERT_EQUAL_UINT(0, s.written.size());  // never transmit over the tape (R1)
    TEST_ASSERT_TRUE(rig.gaveBack());
}

// --- 6. After a safe give-back (power-off-like long COMMAND), a later status frame to D1 is received and
// answered: ACK, then COMPLETE and the 4 status bytes. Control GREEN.
void test_after_give_back_a_later_status_frame_is_answered()
{
    Rig rig;
    rig.tapeOwnsWithMotorOff();
    auto& s = fake_idf::state();
    pulseFromNow(1 * kMs, 30 * kMs);  // longer than the window: seen by the arbiter
    const int64_t longEnd = s.commandLowUntilUs;
    rig.pass(true);
    TEST_ASSERT_TRUE(rig.gaveBack());
    // Review of bf04542 (H2): the next command only after this one is over. Steps go on until the long
    // COMMAND is released and seen high; its window is never overwritten while active.
    for (int i = 0; i < 20 && (s.nowUs < longEnd || !rig.line.commandHigh()); ++i) {
        rig.pass(true);
    }
    TEST_ASSERT_TRUE(s.nowUs >= longEnd);
    TEST_ASSERT_TRUE(rig.line.commandHigh());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DataInOwner::Disk), static_cast<int>(rig.arbiter.owner()));
    TEST_ASSERT_EQUAL_UINT32(0, rig.line.writeFailures().parkedNow.load());
    s.written.clear();
    s.rx.clear();
    (void)scheduleStatusFrame(s.nowUs + 1 * kMs);  // the previous window ended: nothing is overwritten
    rig.pass(true);
    std::printf("H2|tras la devolucion: trama de estado D1|paso %d|tx %u bytes|primero 0x%02X\n",
                static_cast<int>(rig.lastStep), static_cast<unsigned>(s.written.size()),
                s.written.empty() ? 0u : static_cast<unsigned>(s.written[0]));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(StepOutcome::Completed), static_cast<int>(rig.lastStep));
    // ACK, COMPLETE, then the status frame of a single-density ATR (720 x 128, as in
    // test_status_frame_encoder: 00 FF E0 00) and its checksum: exactly 7 bytes.
    TEST_ASSERT_EQUAL_UINT(7, s.written.size());
    const uint8_t expected[7] = {0x41, 0x43, 0x00, 0xFF, 0xE0, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, s.written.data(), 6);
    TEST_ASSERT_EQUAL_HEX8(rb::sio::sio_checksum(rb::core::Span<const uint8_t>{s.written.data() + 2, 4}),
                           s.written[6]);
}

// --- 7. A release still pending keeps UART2 parked: the disk resumes only after the tape lets go (R1).
void test_a_pending_release_keeps_the_uart_parked()
{
    Rig rig;
    rig.tapeOwnsWithMotorOff();
    rig.tape.releaseResult = LineResult::Pending;
    rig.readings.atariOff = true;
    rig.pass();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DataInOwner::ReleasePending), static_cast<int>(rig.arbiter.owner()));
    rig.pass();
    TEST_ASSERT_EQUAL_UINT32(1, rig.line.writeFailures().parkedNow.load());
    rig.tape.releaseResult = LineResult::Done;
    rig.pass();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DataInOwner::Disk), static_cast<int>(rig.arbiter.owner()));
    TEST_ASSERT_EQUAL_UINT32(0, rig.line.writeFailures().parkedNow.load());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_pulse_1ms_is_not_missed);
    RUN_TEST(test_pulse_2ms_is_not_missed);
    RUN_TEST(test_pulse_3ms_is_not_missed);
    RUN_TEST(test_pulse_4ms_is_not_missed);
    RUN_TEST(test_pulse_6ms_is_not_missed);
    RUN_TEST(test_pulse_8ms_is_not_missed);
    RUN_TEST(test_pulse_10ms_is_not_missed);
    RUN_TEST(test_pulse_20ms_is_not_missed);
    RUN_TEST(test_retries_every_5ms_give_back);
    RUN_TEST(test_retries_every_10ms_give_back);
    RUN_TEST(test_retries_every_20ms_give_back);
    RUN_TEST(test_retries_every_50ms_give_back);
    RUN_TEST(test_released_pulse_is_a_short_frame_and_still_low_is_stuck);
    RUN_TEST(test_a_parked_read_never_ends_as_a_receive_error);
    RUN_TEST(test_tape_ended_with_motor_off_gives_back);
    RUN_TEST(test_gates_closed_give_back);
    RUN_TEST(test_tape_ejected_gives_back);
    RUN_TEST(test_power_off_seen_gives_back);
    RUN_TEST(test_no_command_and_nothing_else_keeps_the_tape);
    RUN_TEST(test_a_3ms_pulse_with_motor_on_keeps_the_tape);
    RUN_TEST(test_a_status_frame_through_the_session_with_motor_off_gives_back);
    RUN_TEST(test_after_give_back_a_later_status_frame_is_answered);
    RUN_TEST(test_a_pending_release_keeps_the_uart_parked);
    return UNITY_END();
}
