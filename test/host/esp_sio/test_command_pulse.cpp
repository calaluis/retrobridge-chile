// Issue #125, H2 (review of afa7c03): a COMMAND pulse that starts and ends inside the SIO step, before the
// arbiter samples COMMAND. Models main/menu_sio.cpp's order per loop pass: first the COMMAND window
// (SioSession::step() -> SioLineTransport::waitCommandFrame(kPollTimeoutUs)), then
// readings.commandHigh = line.commandHigh() and g_arbiter.step(). Real EspSioUart (fake ESP-IDF time),
// real SioLineTransport, real UartDiskLine and DataInArbiter; the tape side is a stub that always succeeds.
// It does NOT run sioTask, the SioSession dispatcher, the menu transport or the cassette task.

#include <cstdint>

#include "fake_idf.hpp"
#include "rb/esp/esp_sio_uart.hpp"
#include "rb/menu/data_in_arbiter.hpp"
#include "rb/sio/sio_line_transport.hpp"
#include "rb/sio/sio_session.hpp"
#include "sio_disk_line.hpp"
#include "unity.h"

using rb::esp::EspSioUart;
using rb::host::DataInArbiter;
using rb::host::DataInOwner;
using rb::host::DataInReadings;
using rb::host::LineResult;

namespace {

constexpr EspSioUart::Pins kPins{39, 33, 21};

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
    LineResult release() noexcept override { return LineResult::Done; }
    void abandon() noexcept override {}
};

struct Rig {
    EspSioUart line{kPins};
    rb::menusio::UartDiskLine disk;
    StubTape tape;
    DataInArbiter arbiter{disk, tape};
    IdfClock clock;
    rb::sio::SioLineTransport transport{line, clock};
    DataInReadings readings;

    Rig()
    {
        TEST_ASSERT_TRUE(line.begin());
        disk.attach(line);
        readings.gatesOpen = true;
        readings.tapeOutput = true;
        readings.tapeOnC = true;
    }

    // One pass of the SIO loop, in main/menu_sio.cpp's order.
    void pass()
    {
        (void)transport.waitCommandFrame(rb::sio::SioSession::kPollTimeoutUs);
        readings.commandHigh = line.commandHigh();
        (void)arbiter.step(rb::host::dataInInputs(readings), static_cast<uint64_t>(fake_idf::state().nowUs));
    }

    // The tape owns DATA IN, the UART is parked, then the game stops MOTOR (issue #125's state).
    void tapeOwnsWithMotorOff()
    {
        readings.motorOn = true;
        readings.commandHigh = true;
        (void)arbiter.step(rb::host::dataInInputs(readings), 0);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(DataInOwner::Tape), static_cast<int>(arbiter.owner()));
        readings.motorOn = false;
    }
};

}  // namespace

void setUp()
{
    fake_idf::reset();
    fake_idf::state().commandPin = 39;
}
void tearDown() {}

// Rule 3 of the arbiter: MOTOR off and COMMAND low give the line back. An Atari command frame holds COMMAND
// low about 3 ms (5 bytes at 19200 baud plus t0/t1). With the UART parked, the read in the COMMAND window
// waits kFrameReadUs (6 ms) and takes nothing, so COMMAND is high again before the arbiter looks.
void test_a_3ms_command_pulse_with_motor_off_gives_data_in_back()
{
    Rig rig;
    rig.tapeOwnsWithMotorOff();
    auto& s = fake_idf::state();
    s.commandLowFromUs = s.nowUs + 1000;
    s.commandLowUntilUs = s.commandLowFromUs + 3000;
    rig.pass();
    TEST_ASSERT_EQUAL_UINT32(1, rig.transport.stats().shortFrames);  // the pulse was seen by the SIO step
    TEST_ASSERT_NOT_EQUAL_INT(static_cast<int>(DataInOwner::Tape), static_cast<int>(rig.arbiter.owner()));
}

// The Atari retries a command frame that gets no answer. Each retry is the same 3 ms pulse.
void test_retried_3ms_pulses_with_motor_off_give_data_in_back()
{
    Rig rig;
    rig.tapeOwnsWithMotorOff();
    auto& s = fake_idf::state();
    for (int retry = 0; retry < 14; ++retry) {
        s.commandLowFromUs = s.nowUs + 1000;
        s.commandLowUntilUs = s.commandLowFromUs + 3000;
        rig.pass();
    }
    TEST_ASSERT_EQUAL_UINT32(14, rig.transport.stats().shortFrames);
    TEST_ASSERT_NOT_EQUAL_INT(static_cast<int>(DataInOwner::Tape), static_cast<int>(rig.arbiter.owner()));
}

// Control: COMMAND held low longer than the window (as at power-off). The arbiter sees it and gives back.
void test_a_command_held_low_past_the_window_gives_data_in_back()
{
    Rig rig;
    rig.tapeOwnsWithMotorOff();
    auto& s = fake_idf::state();
    s.commandLowFromUs = s.nowUs + 1000;
    s.commandLowUntilUs = s.commandLowFromUs + 1000000;
    rig.pass();
    TEST_ASSERT_EQUAL_UINT32(1, rig.transport.stats().stuckLows);
    TEST_ASSERT_NOT_EQUAL_INT(static_cast<int>(DataInOwner::Tape), static_cast<int>(rig.arbiter.owner()));
}

// Control: with MOTOR on, a 3 ms pulse must keep the tape (rule 2), whatever the arbiter sees.
void test_a_3ms_command_pulse_with_motor_on_keeps_the_tape()
{
    Rig rig;
    rig.tapeOwnsWithMotorOff();
    rig.readings.motorOn = true;
    auto& s = fake_idf::state();
    s.commandLowFromUs = s.nowUs + 1000;
    s.commandLowUntilUs = s.commandLowFromUs + 3000;
    rig.pass();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(DataInOwner::Tape), static_cast<int>(rig.arbiter.owner()));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_a_3ms_command_pulse_with_motor_off_gives_data_in_back);
    RUN_TEST(test_retried_3ms_pulses_with_motor_off_give_data_in_back);
    RUN_TEST(test_a_command_held_low_past_the_window_gives_data_in_back);
    RUN_TEST(test_a_3ms_command_pulse_with_motor_on_keeps_the_tape);
    return UNITY_END();
}
