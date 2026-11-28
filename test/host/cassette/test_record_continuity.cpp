// Issue #126, hypothesis H-A: inside a record the UART must never go idle between two bytes.
//
// The Atari OS measures the tape speed on the two sync bytes (0x55 0x55) that start every record, and
// the STAC blocks of the tape in #126 start the same way. An idle mark between those bytes stretches
// the measurement. CassettePlayer hands a byte to ByteOut only once its whole slot has passed, and the
// cassette loop ticks every 2 ms (cassette_service.cpp kLoopPeriod), so the UART can run dry between
// two bytes of the same record.
//
// These tests model the UART: it shifts 10 bits per byte at the rate set by setBaud() and starts a byte
// at max(now, end of the previous byte). They record every idle time between two bytes of one record.
// RED today shows only that the gap exists in the model. It does NOT show that the gap causes the BOOT
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

// One record shaped like the tape in #126: 0x55 0x55, then a fill byte.
Bytes record(std::size_t length)
{
    Bytes body(length, 0xA5);
    body[0] = 0x55;
    body[1] = 0x55;
    return body;
}

Bytes tape(uint32_t baud, uint16_t gapMs, std::size_t length)
{
    Bytes out = chunk("FUJI", 0, {});
    if (baud != 600) {
        out = out + chunk("baud", static_cast<uint16_t>(baud), {});
    }
    return out + chunk("data", gapMs, record(length));
}

class OnMotor final : public MotorIn {
public:
    bool on() const override { return true; }
};

class NoWave final : public WaveOut {
public:
    bool push(const FskPortion&) override { return true; }
    bool idle() const override { return true; }
    void stop() override {}
};

// A UART that shifts each byte for 10 bit times. Times are kept in us * baud, so nothing is rounded.
class ModelUart final : public ByteOut {
public:
    uint64_t nowUs = 0;
    uint32_t baud = 600;
    uint64_t busyUntil = 0;   // us * baud: when the last byte handed over has left
    uint32_t bytesSent = 0;
    std::vector<uint64_t> idleBefore;  // idle time before each byte after the first, us * baud

    bool setBaud(uint32_t rate) override
    {
        baud = rate;
        return true;
    }
    bool send(Span<const uint8_t> bytes) override
    {
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            const uint64_t now = nowUs * baud;
            uint64_t start = busyUntil;
            if (bytesSent == 0 || now > busyUntil) {
                start = now;
            }
            if (bytesSent > 0) {
                idleBefore.push_back(start - busyUntil);
            }
            busyUntil = start + kByteCost;
            ++bytesSent;
        }
        return true;
    }
    bool drained() const override { return nowUs * baud >= busyUntil; }

    // Longest idle time between two bytes, in microseconds (rounded down).
    uint64_t longestIdleUs() const
    {
        uint64_t longest = 0;
        for (uint64_t idle : idleBefore) {
            longest = idle > longest ? idle : longest;
        }
        return longest / baud;
    }
};

CasSegment g_table[16];
uint8_t g_bank[4096];

// Plays a one-record tape with the cassette loop's tick: every kLoopUs, first tick at `phaseUs`.
void play(const Bytes& file, ModelUart& uart, uint64_t phaseUs)
{
    MemoryTapeSource source(Span<const uint8_t>(file.data(), file.size()));
    const CasIndexResult index = CasIndex::build(source, Span<CasSegment>(g_table, 16));
    TEST_ASSERT_EQUAL(static_cast<int>(CasStatus::Ok), static_cast<int>(index.status));
    FskLoader loader;
    loader.bind(Span<uint8_t>(g_bank, sizeof g_bank));
    CassettePlayer player(loader);
    OnMotor motor;
    NoWave wave;
    player.load(source, Span<const CasSegment>(g_table, index.count), wave);
    player.tick(0, motor, uart, wave);
    for (uint64_t now = phaseUs; !player.atEnd() && now < 60000000u; now += kLoopUs) {
        uart.nowUs = now;
        player.tick(now, motor, uart, wave);
    }
    TEST_ASSERT_TRUE(player.atEnd());
}

}  // namespace

void setUp() {}
void tearDown() {}

// ---- the gauge itself: it must pass on back-to-back bytes and fail on a gap ----

void test_gauge_reads_no_idle_for_back_to_back_bytes()
{
    ModelUart uart;
    const uint8_t two[2] = {0x55, 0x55};
    uart.send(Span<const uint8_t>(two, 2));
    TEST_ASSERT_EQUAL_UINT32(1, uart.idleBefore.size());
    TEST_ASSERT_EQUAL_UINT64(0, uart.longestIdleUs());
}

void test_gauge_reads_the_idle_of_a_late_byte()
{
    ModelUart uart;
    const uint8_t one[1] = {0x55};
    uart.send(Span<const uint8_t>(one, 1));
    uart.nowUs = 16667 + 2000;  // one byte at 600 baud is 16666.7 us; the next comes 2 ms late
    uart.send(Span<const uint8_t>(one, 1));
    TEST_ASSERT_EQUAL_UINT64(2000, uart.longestIdleUs());
}

// ---- H-A: the player against the model ----

// The two sync bytes of a 600-baud OS record (the four boot records of the tape in #126: 132 bytes,
// 238 ms gap) leave the UART with no idle between them, whatever the loop's phase.
void test_sync_bytes_back_to_back_at_600()
{
    for (uint64_t phase = 0; phase < kLoopUs; phase += 250) {
        ModelUart uart;
        play(tape(600, 238, 132), uart, phase);
        TEST_ASSERT_TRUE(uart.idleBefore.size() > 0);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, uart.idleBefore[0] / uart.baud, "idle between the two 0x55 at 600");
    }
}

// The same for a 750-baud STAC block (257 bytes, 262 ms gap).
void test_sync_bytes_back_to_back_at_750()
{
    for (uint64_t phase = 0; phase < kLoopUs; phase += 250) {
        ModelUart uart;
        play(tape(750, 262, 257), uart, phase);
        TEST_ASSERT_TRUE(uart.idleBefore.size() > 0);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, uart.idleBefore[0] / uart.baud, "idle between the two 0x55 at 750");
    }
}

// No idle anywhere inside a record: the UART is never dry from its first byte to its last.
void test_no_idle_inside_a_record()
{
    ModelUart uart600;
    play(tape(600, 238, 132), uart600, 0);
    TEST_ASSERT_EQUAL_UINT32(131, uart600.idleBefore.size());
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, uart600.longestIdleUs(), "longest idle inside a 600-baud record, us");

    ModelUart uart750;
    play(tape(750, 262, 257), uart750, 0);
    TEST_ASSERT_EQUAL_UINT32(256, uart750.idleBefore.size());
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0, uart750.longestIdleUs(), "longest idle inside a 750-baud record, us");
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_gauge_reads_no_idle_for_back_to_back_bytes);
    RUN_TEST(test_gauge_reads_the_idle_of_a_late_byte);
    RUN_TEST(test_sync_bytes_back_to_back_at_600);
    RUN_TEST(test_sync_bytes_back_to_back_at_750);
    RUN_TEST(test_no_idle_inside_a_record);
    return UNITY_END();
}
