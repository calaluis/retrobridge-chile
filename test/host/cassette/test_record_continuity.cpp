// Issue #126, hypothesis H-A: inside a record the UART must never go idle between two bytes
// (spec cassette-cas, "Bytes seguidos", AC14).
//
// The Atari OS measures the tape speed on the two sync bytes (0x55 0x55) that start every record, and
// the STAC blocks of the tape in #126 start the same way. An idle mark between those bytes stretches
// the measurement. The cassette loop ticks every 2 ms (cassette_service.cpp kLoopPeriod).
//
// These tests model the UART: it shifts 10 bits per byte at the rate set by setBaud() and starts a byte
// at max(now, end of the previous byte). They check, against the model:
// - no idle between two bytes of a record, the two 0x55 included, whatever the loop's phase;
// - the next gap starts after the last byte has left the line, and lasts the file's gap (plus at most one
//   loop period, the polling of drained());
// - the player never has more than kLeadBytes + 1 bytes on their way (decision 3 of EspDataIn allows 32);
// - MOTOR off in a record: nothing more is handed over, nothing is lost or sent twice when it comes back.
// GREEN shows only that the player meets this in the model. It does NOT show that the idle caused the BOOT
// ERROR or the CORRECCION messages in #126: that needs the A/B test on the 65XE.

#include <cstdint>
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
using rb::cassette::MemoryTapeSource;
using rb::cassette::MotorIn;
using rb::cassette::WaveOut;
using rb::core::Span;

namespace {

using Bytes = std::vector<uint8_t>;

constexpr uint64_t kLoopUs = 2000;              // cassette_service.cpp: kLoopPeriod = 2 ms
constexpr uint64_t kByteCost = 10u * 1000000u;  // 10 bits per byte, in us * baud
// The model rounds each byte's time down to the ns: "no idle" is under 1 us (one bit is 1333 us or more).
constexpr uint64_t kRoundingNs = 1000;

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

// One record shaped like the tape in #126: 0x55 0x55, then numbered bytes (to see a loss or a repeat).
Bytes record(std::size_t length, uint8_t first)
{
    Bytes body(length, 0);
    for (std::size_t i = 0; i < length; ++i) {
        body[i] = static_cast<uint8_t>(first + i);
    }
    body[0] = 0x55;
    body[1] = 0x55;
    return body;
}

Bytes header(uint32_t baud)
{
    Bytes out = chunk("FUJI", 0, {});
    if (baud != 600) {
        out = out + chunk("baud", static_cast<uint16_t>(baud), {});
    }
    return out;
}

Bytes tape(uint32_t baud, uint16_t gapMs, std::size_t length)
{
    return header(baud) + chunk("data", gapMs, record(length, 0x10));
}

class SwitchMotor final : public MotorIn {
public:
    bool motor = true;
    bool on() const override { return motor; }
};

class NoWave final : public WaveOut {
public:
    bool push(const FskPortion&) override { return true; }
    bool idle() const override { return true; }
    void stop() override {}
};

// A UART that shifts each byte for 10 bit times. Times are kept in ns, from us * baud.
class ModelUart final : public ByteOut {
public:
    struct Byte {
        uint8_t value;
        uint64_t startNs;
        uint64_t endNs;
    };
    uint64_t nowUs = 0;
    uint32_t baud = 600;
    std::vector<Byte> line;
    std::size_t mostOnTheWay = 0;  // bytes handed over and not yet out, at the end of each send()

    bool setBaud(uint32_t rate) override
    {
        baud = rate;
        return true;
    }
    bool send(Span<const uint8_t> bytes) override
    {
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            const uint64_t now = nowUs * 1000u;
            uint64_t start = line.empty() || line.back().endNs < now ? now : line.back().endNs;
            line.push_back({bytes[i], start, start + kByteCost * 1000u / baud});
        }
        const std::size_t onTheWay = this->onTheWay();
        mostOnTheWay = onTheWay > mostOnTheWay ? onTheWay : mostOnTheWay;
        return true;
    }
    bool drained() const override { return line.empty() || line.back().endNs <= nowUs * 1000u; }

    std::size_t onTheWay() const
    {
        std::size_t n = 0;
        for (const Byte& b : line) {
            n += b.endNs > nowUs * 1000u ? 1u : 0u;
        }
        return n;
    }
    // Idle before byte `i`, the byte before it being of the same record.
    uint64_t idleBeforeNs(std::size_t i) const { return line[i].startNs - line[i - 1].endNs; }
    uint64_t longestIdleNs(std::size_t from, std::size_t to) const
    {
        uint64_t longest = 0;
        for (std::size_t i = from + 1; i < to; ++i) {
            longest = idleBeforeNs(i) > longest ? idleBeforeNs(i) : longest;
        }
        return longest;
    }
};

CasSegment g_table[16];
uint8_t g_bank[4096];

struct Deck {
    Bytes file;  // owned: the source reads it for the whole test
    MemoryTapeSource source;
    FskLoader loader;
    CassettePlayer player{loader};
    SwitchMotor motor;
    NoWave wave;
    ModelUart uart;
    uint64_t now = 0;

    explicit Deck(const Bytes& tape) : file(tape), source(Span<const uint8_t>(file.data(), file.size()))
    {
        const CasIndexResult index = CasIndex::build(source, Span<CasSegment>(g_table, 16));
        TEST_ASSERT_EQUAL(static_cast<int>(CasStatus::Ok), static_cast<int>(index.status));
        loader.bind(Span<uint8_t>(g_bank, sizeof g_bank));
        player.load(source, Span<const CasSegment>(g_table, index.count), wave);
        player.tick(0, motor, uart, wave);
    }
    void tickAt(uint64_t us)
    {
        now = us;
        uart.nowUs = us;
        player.tick(us, motor, uart, wave);
    }
    // The cassette loop: a tick every kLoopUs from `firstUs`, until the tape ends or `untilUs`.
    void run(uint64_t firstUs, uint64_t untilUs = 60000000u)
    {
        for (uint64_t t = firstUs; !player.atEnd() && t < untilUs; t += kLoopUs) {
            tickAt(t);
        }
    }
};

}  // namespace

void setUp() {}
void tearDown() {}

// ---- the gauge itself: it must pass on back-to-back bytes and fail on a gap ----

void test_gauge_reads_no_idle_for_back_to_back_bytes()
{
    ModelUart uart;
    const uint8_t two[2] = {0x55, 0x55};
    uart.send(Span<const uint8_t>(two, 2));
    TEST_ASSERT_EQUAL_UINT64(0, uart.idleBeforeNs(1));
    TEST_ASSERT_EQUAL_UINT32(2, uart.mostOnTheWay);
}

void test_gauge_reads_the_idle_of_a_late_byte()
{
    ModelUart uart;
    const uint8_t one[1] = {0x55};
    uart.send(Span<const uint8_t>(one, 1));
    uart.nowUs = 16667 + 2000;  // one byte at 600 baud is 16666.7 us; the next comes 2 ms late
    uart.send(Span<const uint8_t>(one, 1));
    TEST_ASSERT_UINT64_WITHIN(1000, 2000000, uart.idleBeforeNs(1));
    TEST_ASSERT_FALSE(uart.drained());
    uart.nowUs = 2 * 16667 + 2000;
    TEST_ASSERT_TRUE(uart.drained());
}

// ---- AC14: the player against the model ----

// The two sync bytes of a 600-baud OS record (the four boot records of the tape in #126: 132 bytes,
// 238 ms gap) and of a 750-baud STAC block (257 bytes, 262 ms gap): no idle, whatever the loop's phase.
void test_sync_bytes_back_to_back_at_600_and_750()
{
    for (uint64_t phase = 0; phase < kLoopUs; phase += 250) {
        Deck d600(tape(600, 238, 132));
        d600.run(phase);
        TEST_ASSERT_TRUE(d600.player.atEnd());
        TEST_ASSERT_LESS_THAN_UINT64_MESSAGE(kRoundingNs, d600.uart.idleBeforeNs(1), "idle between the two 0x55 at 600");

        Deck d750(tape(750, 262, 257));
        d750.run(phase);
        TEST_ASSERT_TRUE(d750.player.atEnd());
        TEST_ASSERT_LESS_THAN_UINT64_MESSAGE(kRoundingNs, d750.uart.idleBeforeNs(1), "idle between the two 0x55 at 750");
    }
}

// No idle anywhere inside a record, and every byte of the file, once, in order.
void test_no_idle_inside_a_record()
{
    for (uint64_t phase = 0; phase < kLoopUs; phase += 250) {
        Deck d(tape(600, 238, 132));
        d.run(phase);
        TEST_ASSERT_EQUAL_UINT32(132, d.uart.line.size());
        TEST_ASSERT_LESS_THAN_UINT64_MESSAGE(kRoundingNs, d.uart.longestIdleNs(0, 132), "idle inside a 600-baud record");
        const Bytes want = record(132, 0x10);
        for (std::size_t i = 0; i < want.size(); ++i) {
            TEST_ASSERT_EQUAL_UINT8(want[i], d.uart.line[i].value);
        }
    }
}

// The first byte starts when the gap ends: the gap from tape start to the first byte is the file's.
void test_first_byte_starts_when_the_gap_ends()
{
    Deck d(tape(600, 238, 132));
    d.run(kLoopUs);
    const uint64_t startNs = d.uart.line[0].startNs;
    TEST_ASSERT_TRUE_MESSAGE(startNs >= 238000000u, "first byte before its gap ended");
    TEST_ASSERT_TRUE_MESSAGE(startNs <= 238000000u + kLoopUs * 1000u, "first byte more than one loop late");
}

// The next gap starts after the last byte has left the line and lasts the file's gap (polling adds at most
// one loop period), at 600 and with a rate change to 750 between the records.
void test_next_gap_starts_after_the_last_byte_has_left()
{
    for (uint64_t phase = 0; phase < kLoopUs; phase += 250) {
        Deck d(header(600) + chunk("data", 238, record(132, 0x10)) + chunk("data", 238, record(132, 0x90)));
        d.run(phase);
        TEST_ASSERT_EQUAL_UINT32(264, d.uart.line.size());
        const uint64_t gapNs = d.uart.line[132].startNs - d.uart.line[131].endNs;
        TEST_ASSERT_TRUE_MESSAGE(gapNs >= 238000000u, "600: gap shorter than the file's");
        TEST_ASSERT_TRUE_MESSAGE(gapNs <= 238000000u + kLoopUs * 1000u, "600: gap longer than one loop over");

        Deck r(header(600) + chunk("data", 238, record(132, 0x10)) + chunk("baud", 750, {}) +
               chunk("data", 262, record(257, 0x90)));
        r.run(phase);
        TEST_ASSERT_EQUAL_UINT32(132 + 257, r.uart.line.size());
        const uint64_t gap750 = r.uart.line[132].startNs - r.uart.line[131].endNs;
        TEST_ASSERT_TRUE_MESSAGE(gap750 >= 262000000u, "750: gap shorter than the file's");
        TEST_ASSERT_TRUE_MESSAGE(gap750 <= 262000000u + kLoopUs * 1000u, "750: gap longer than one loop over");
        TEST_ASSERT_LESS_THAN_UINT64_MESSAGE(kRoundingNs, r.uart.longestIdleNs(132, 132 + 257), "idle inside the 750 record");
    }
}

// Never more than kLeadBytes + 1 bytes on their way (EspDataIn's shared mode allows 32).
void test_bytes_on_their_way_stay_bounded()
{
    Deck d600(tape(600, 238, 132));
    d600.run(0);
    TEST_ASSERT_TRUE(d600.uart.mostOnTheWay <= CassettePlayer::kLeadBytes + 1);
    Deck d750(tape(750, 262, 257));
    d750.run(0);
    TEST_ASSERT_TRUE(d750.uart.mostOnTheWay <= CassettePlayer::kLeadBytes + 1);
    TEST_ASSERT_TRUE(CassettePlayer::kLeadBytes + 1 <= 32);
}

// MOTOR off in a record: nothing more is handed over while it is off; at most kLeadBytes + 1 bytes were
// still on their way; when it comes back the record goes on with the next byte, none lost or sent twice.
void test_motor_off_in_a_record_hands_nothing_more()
{
    Deck d(tape(600, 0, 40));
    d.run(0, 200000);  // 0.2 s: about 12 bytes out
    const std::size_t handed = d.uart.line.size();
    TEST_ASSERT_TRUE(handed > 2 && handed < 40);
    TEST_ASSERT_TRUE(d.uart.onTheWay() <= CassettePlayer::kLeadBytes + 1);
    d.motor.motor = false;
    for (uint64_t t = 200000; t < 1200000; t += kLoopUs) {
        d.tickAt(t);
    }
    TEST_ASSERT_EQUAL_UINT32(handed, d.uart.line.size());
    d.motor.motor = true;
    d.run(1200000);
    TEST_ASSERT_TRUE(d.player.atEnd());
    const Bytes want = record(40, 0x10);
    TEST_ASSERT_EQUAL_UINT32(want.size(), d.uart.line.size());
    for (std::size_t i = 0; i < want.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(want[i], d.uart.line[i].value);
    }
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_gauge_reads_no_idle_for_back_to_back_bytes);
    RUN_TEST(test_gauge_reads_the_idle_of_a_late_byte);
    RUN_TEST(test_sync_bytes_back_to_back_at_600_and_750);
    RUN_TEST(test_no_idle_inside_a_record);
    RUN_TEST(test_first_byte_starts_when_the_gap_ends);
    RUN_TEST(test_next_gap_starts_after_the_last_byte_has_left);
    RUN_TEST(test_bytes_on_their_way_stay_bounded);
    RUN_TEST(test_motor_off_in_a_record_hands_nothing_more);
    return UNITY_END();
}
