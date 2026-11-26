// FujiTapeRunner (cassette-motor-fujinet): FujiTapeLoop driven from the cassette loop. The enable rules are
// RetroBridge's assumptions (docs/diseno/cassette-motor-fujinet.md §3): MOTOR on enables at 600 baud, MOTOR
// off between records keeps the place, the end of the tape waits for MOTOR off and on, a new mount, eject or
// rewind starts over. The UART may be busy: setBaud and send are retried, flush waits for drained.
#include <vector>

#include "rb/cassette/cassette_player.hpp"
#include "rb/cassette/tape_source.hpp"
#include "rb/fujinet/fuji_tape_runner.hpp"
#include "unity.h"

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
    unsigned busySends = 0;  // send() refuses this many times first
    mutable unsigned undrained = 0;  // drained() is false this many times first
    unsigned refusals = 0;

    bool setBaud(uint32_t baud) override
    {
        bauds.push_back(baud);
        return true;
    }
    bool send(rb::core::Span<const uint8_t> bytes) override
    {
        if (busySends > 0) {
            --busySends;
            ++refusals;
            return false;
        }
        sent.insert(sent.end(), bytes.data(), bytes.data() + bytes.size());
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
    unsigned yields = 0;
    void waitOneMs() override { ++ms; }
    void yield() override { ++yields; }
};

class NoFsk final : public rb::fujinet::FskRunPlayer {
public:
    rb::fujinet::FskRunOutcome play(uint32_t, uint16_t) override { return {}; }
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

}  // namespace

// No tape, or MOTOR off: nothing plays and nothing is touched.
void test_nothing_plays_without_tape_or_motor()
{
    const Bytes f = twoRecords();
    MemoryTapeSource src({f.data(), f.size()});
    Out out;
    Motor motor;
    Pause pause;
    NoFsk fsk;
    FujiTapeRunner runner(out, motor, fsk, pause);
    motor.level = true;
    TEST_ASSERT_FALSE(runner.tick());  // no tape yet
    TEST_ASSERT_TRUE(runner.follow(&src, 1));
    motor.level = false;
    TEST_ASSERT_FALSE(runner.tick());
    TEST_ASSERT_TRUE(out.bauds.empty());
    TEST_ASSERT_TRUE(out.sent.empty());
}

// MOTOR on: enabled at 600 baud, one record per tick, in order; the end waits for MOTOR off and on, then the
// tape starts again from its first record.
void test_records_play_one_per_tick_and_the_end_waits_for_motor()
{
    const Bytes f = twoRecords();
    MemoryTapeSource src({f.data(), f.size()});
    Out out;
    Motor motor;
    Pause pause;
    NoFsk fsk;
    FujiTapeRunner runner(out, motor, fsk, pause);
    runner.follow(&src, 1);
    motor.level = true;

    TEST_ASSERT_TRUE(runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Data, runner.last().kind);
    TEST_ASSERT_EQUAL(1, out.bauds.size());
    TEST_ASSERT_EQUAL_UINT32(600, out.bauds[0]);
    TEST_ASSERT_TRUE(runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Data, runner.last().kind);
    TEST_ASSERT_EQUAL_UINT32(750, out.bauds.back());
    TEST_ASSERT_TRUE(runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::End, runner.last().kind);
    TEST_ASSERT_FALSE(runner.enabled());
    TEST_ASSERT_EQUAL(5, out.sent.size());
    TEST_ASSERT_EQUAL_UINT(3 + 2, pause.ms);

    TEST_ASSERT_FALSE(runner.tick());  // MOTOR still on: waits
    motor.level = false;
    TEST_ASSERT_FALSE(runner.tick());
    motor.level = true;
    TEST_ASSERT_TRUE(runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Data, runner.last().kind);
    TEST_ASSERT_EQUAL(8, out.sent.size());
    TEST_ASSERT_EQUAL_UINT8(1, out.sent[5]);
}

// MOTOR off between records: the tape keeps its place; MOTOR on again re-enables at 600 baud.
void test_motor_off_between_records_keeps_the_place()
{
    const Bytes f = twoRecords();
    MemoryTapeSource src({f.data(), f.size()});
    Out out;
    Motor motor;
    Pause pause;
    NoFsk fsk;
    FujiTapeRunner runner(out, motor, fsk, pause);
    runner.follow(&src, 1);
    motor.level = true;
    TEST_ASSERT_TRUE(runner.tick());
    const uint32_t place = runner.offset();
    motor.level = false;
    TEST_ASSERT_FALSE(runner.tick());
    TEST_ASSERT_EQUAL_UINT32(place, runner.offset());
    motor.level = true;
    TEST_ASSERT_TRUE(runner.tick());
    TEST_ASSERT_EQUAL(3, out.bauds.size());  // 600, 600 again, then the tape's 750
    TEST_ASSERT_EQUAL_UINT32(600, out.bauds[1]);
    TEST_ASSERT_EQUAL_UINT8(4, out.sent[3]);
}

// A new generation (mount, eject, rewind) starts the tape over; the same one changes nothing.
void test_new_generation_starts_over()
{
    const Bytes f = twoRecords();
    MemoryTapeSource src({f.data(), f.size()});
    Out out;
    Motor motor;
    Pause pause;
    NoFsk fsk;
    FujiTapeRunner runner(out, motor, fsk, pause);
    TEST_ASSERT_TRUE(runner.follow(&src, 1));
    motor.level = true;
    TEST_ASSERT_TRUE(runner.tick());
    TEST_ASSERT_FALSE(runner.follow(&src, 1));
    TEST_ASSERT_TRUE(runner.offset() > 0);
    TEST_ASSERT_TRUE(runner.follow(&src, 2));
    TEST_ASSERT_EQUAL_UINT32(0, runner.offset());
    TEST_ASSERT_TRUE(runner.tick());
    TEST_ASSERT_EQUAL_UINT8(1, out.sent[3]);  // the first record again
    TEST_ASSERT_TRUE(runner.follow(nullptr, 3));
    TEST_ASSERT_FALSE(runner.tick());
}

// A busy UART: send is retried until it takes the piece, flush waits until the line is empty.
void test_busy_uart_is_waited_for()
{
    const Bytes f = twoRecords();
    MemoryTapeSource src({f.data(), f.size()});
    Out out;
    out.busySends = 3;
    out.undrained = 2;
    Motor motor;
    Pause pause;
    NoFsk fsk;
    FujiTapeRunner runner(out, motor, fsk, pause);
    runner.follow(&src, 1);
    motor.level = true;
    TEST_ASSERT_TRUE(runner.tick());
    TEST_ASSERT_EQUAL(3, out.sent.size());
    TEST_ASSERT_EQUAL_UINT(3, out.refusals);
    TEST_ASSERT_EQUAL_UINT(3 + 2, pause.yields);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_nothing_plays_without_tape_or_motor);
    RUN_TEST(test_records_play_one_per_tick_and_the_end_waits_for_motor);
    RUN_TEST(test_motor_off_between_records_keeps_the_place);
    RUN_TEST(test_new_generation_starts_over);
    RUN_TEST(test_busy_uart_is_waited_for);
    return UNITY_END();
}
