// The owner's FujiNet FSK engine as it is (cassetteFSK.cpp, cassetteFSKLoader.cpp; beta
// v1.6.2-atari-fsk-beta.1, commit cf3382f) behind RetroBridge's adapters (EspFskEngine, FujiTapeLoop), over
// fake_idf (cassette-motor-fujinet). The RMT plays a batch of symbols each time the engine waits for it.
//
// - Whole chain on real tapes (RB_CAS_SAMPLES): FujiTapeLoop + EspFskEngine. Every byte on the UART and every
//   portion on the RMT, in order, against CasIndex + FskWave (RetroBridge's own reading of the same file).
// - Progressive loading (first chunk of 16 384 bytes or more): the loader task is a host thread.
// - MOTOR drop mid-run: the pin goes back to the UART while the RMT channel is still enabled (D8), the RMT
//   memory abort runs, the run is not played.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "driver/rmt_encoder.h"
#include "fake_idf.hpp"
#include "rb/cassette/cas_index.hpp"
#include "rb/cassette/fsk_wave.hpp"
#include "rb/cassette/tape_source.hpp"
#include "rb/fujinet/esp_fsk_engine.hpp"
#include "rb/fujinet/fuji_tape_loop.hpp"
#include "unity.h"

extern "C" volatile uint32_t RMTMEM[512];

using rb::cassette::CasSegment;
using rb::cassette::FskPortion;
using rb::cassette::MemoryTapeSource;
using rb::fujinet::EspFskEngine;
using rb::fujinet::FujiStepKind;
using rb::fujinet::FujiTapeLoop;
using Bytes = std::vector<uint8_t>;

namespace {

constexpr int kDataIn = 21;
constexpr uint8_t kStatusOk = 0;
constexpr uint8_t kStatusAllocationFailed = 2;
constexpr uint8_t kStatusMotorDropped = 5;

// What reached the SIO DATA IN line, in order: UART bytes or RMT portions.
struct Line {
    bool uart;
    uint8_t byte;        // uart
    FskPortion portion;  // rmt
};

std::vector<Line> g_line;
std::size_t g_batch = 512;           // symbols the RMT plays per wait
unsigned g_waits = 0;
unsigned g_dropAtWait = 0;           // 0: never; else MOTOR reads dropped from this wait on
bool g_sleepPerWait = false;         // progressive: give the loader thread real time
fake_idf::Owner g_ownerAtDrain = fake_idf::Owner::None;
bool g_enabledAtDrain = false;
bool g_dropped = false;

void recordSymbols(const void* symbols, std::size_t count)
{
    const auto* s = static_cast<const rmt_symbol_word_t*>(symbols);
    for (std::size_t i = 0; i < count; ++i) {
        if (s[i].duration0 > 0) {
            g_line.push_back({false, 0, {s[i].level0 != 0, s[i].duration0}});
        }
        if (s[i].duration1 > 0) {
            g_line.push_back({false, 0, {s[i].level1 != 0, s[i].duration1}});
        }
    }
}

void onWait()
{
    ++g_waits;
    if (g_sleepPerWait) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto& s = fake_idf::state();
    if (g_dropped && s.rmtBusy) {
        g_ownerAtDrain = s.owner;  // after the stop: who has the pin while the channel is still enabled
        g_enabledAtDrain = s.rmtEnabled;
    }
    fake_idf::play_symbols(g_batch);
}

bool motorDropped(void*)
{
    if (g_dropAtWait != 0 && g_waits >= g_dropAtWait) {
        g_dropped = true;
        return true;
    }
    return false;
}

void flushUart() {}

// The UART side of the chain: bytes to the line, MOTOR from the same switch as the RMT side.
class Bus final : public rb::fujinet::FujiTapeBus {
public:
    uint32_t baud = 0;
    void setBaud(uint32_t b) override { baud = b; }
    void write(const uint8_t* bytes, uint32_t count) override
    {
        for (uint32_t i = 0; i < count; ++i) {
            g_line.push_back({true, bytes[i], {}});
        }
    }
    void flushOutput() override {}
    void waitOneMs() override {}
    bool motorDropped() const override { return false; }
};

bool readFile(const std::string& path, Bytes& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

uint16_t le16(const Bytes& f, std::size_t at) { return static_cast<uint16_t>(f[at] | (f[at + 1] << 8)); }

// The payload of an fsk run (its chunks joined), and where the run ends.
Bytes runPayload(const Bytes& f, const CasSegment& seg, uint32_t& end)
{
    Bytes values;
    uint32_t at = seg.offset;
    for (unsigned c = 0; c < seg.chunks; ++c) {
        const uint16_t len = le16(f, at + 4);
        values.insert(values.end(), f.begin() + at + 8, f.begin() + at + 8 + len);
        at += 8 + len;
    }
    end = at;
    return values;
}

std::vector<FskPortion> expectedPortions(const Bytes& values)
{
    std::vector<FskPortion> out;
    rb::cassette::FskWave wave;
    wave.reset();
    wave.setValues({values.data(), values.size()});
    FskPortion p;
    while (wave.next(p)) {
        out.push_back(p);
    }
    return out;
}

void resetAll()
{
    fake_idf::reset();
    auto& s = fake_idf::state();
    s.owner = fake_idf::Owner::Uart;  // the UART has DATA IN before a run
    s.outSel = fake_idf::kU2TxdOutIdx;
    s.pin = kDataIn;
    s.onRmtWait = &onWait;
    s.onSymbols = &recordSymbols;
    g_line.clear();
    g_batch = 512;
    g_waits = 0;
    g_dropAtWait = 0;
    g_sleepPerWait = false;
    g_ownerAtDrain = fake_idf::Owner::None;
    g_enabledAtDrain = false;
    g_dropped = false;
    rb::fujinet::setDataInPin(kDataIn);
    rb::fujinet::setFlushOutput(&flushUart);
}

void assertPinBackOnUart()
{
    const auto& s = fake_idf::state();
    TEST_ASSERT_TRUE(s.owner == fake_idf::Owner::Uart);
    TEST_ASSERT_EQUAL_UINT32(fake_idf::kU2TxdOutIdx, s.outSel);
    TEST_ASSERT_FALSE(s.rmtChannel);
    TEST_ASSERT_FALSE(s.rmtEnabled);
}

// The whole tape through FujiTapeLoop and the owner's engine; the line against CasIndex + FskWave.
void playWholeTape(const char* name, bool progressiveExpected)
{
    const char* samples = std::getenv("RB_CAS_SAMPLES");
    if (samples == nullptr) {
        TEST_IGNORE_MESSAGE("RB_CAS_SAMPLES not set");
    }
    Bytes f;
    if (!readFile(std::string(samples) + "/" + name, f)) {
        TEST_IGNORE_MESSAGE("tape not found");
    }
    resetAll();
    g_sleepPerWait = progressiveExpected;
    MemoryTapeSource src({f.data(), f.size()});
    std::vector<CasSegment> table(4096);
    const auto built = rb::cassette::CasIndex::build(src, {table.data(), table.size()});
    TEST_ASSERT_EQUAL(rb::cassette::CasStatus::Ok, built.status);

    std::vector<Line> expected;
    bool progressive = false;
    unsigned fskRuns = 0;
    for (uint32_t i = 0; i < built.count; ++i) {
        const CasSegment& seg = table[i];
        if (seg.kind == rb::cassette::SegmentKind::Data) {
            for (uint32_t b = 0; b < seg.length; ++b) {
                expected.push_back({true, f[seg.offset + b], {}});
            }
        } else {
            ++fskRuns;
            progressive = progressive || le16(f, seg.offset + 4) >= 16384;
            uint32_t end = 0;
            for (const FskPortion& p : expectedPortions(runPayload(f, seg, end))) {
                expected.push_back({false, 0, p});
            }
        }
    }
    TEST_ASSERT_EQUAL(progressiveExpected, progressive);

    EspFskEngine engine;
    TEST_ASSERT_TRUE(engine.ready());
    engine.attach(&src, &motorDropped, nullptr);
    Bus bus;
    FujiTapeLoop loop(src, bus, engine);
    loop.enable();
    unsigned steps = 0;
    for (;;) {
        const rb::fujinet::FujiStep step = loop.step();
        TEST_ASSERT_TRUE_MESSAGE(step.kind != FujiStepKind::Replay, "a run did not play");
        if (step.kind == FujiStepKind::End) {
            break;
        }
        if (step.kind == FujiStepKind::Fsk) {
            TEST_ASSERT_EQUAL_UINT8(kStatusOk, engine.lastStatus());
            assertPinBackOnUart();
        }
        TEST_ASSERT_TRUE(++steps <= built.count);
    }
    TEST_ASSERT_EQUAL_UINT32(built.count, steps);
    engine.stop();

    TEST_ASSERT_EQUAL_UINT32(expected.size(), g_line.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const Line& e = expected[i];
        const Line& g = g_line[i];
        if (e.uart != g.uart || (e.uart && e.byte != g.byte) ||
            (!e.uart && (e.portion.level != g.portion.level || e.portion.ticks != g.portion.ticks))) {
            char msg[96];
            std::snprintf(msg, sizeof msg, "line differs at item %u", static_cast<unsigned>(i));
            TEST_FAIL_MESSAGE(msg);
        }
    }
    const auto& s = fake_idf::state();
    TEST_ASSERT_EQUAL_UINT(s.heapAllocs, s.heapFrees);
    TEST_ASSERT_EQUAL_UINT(0, s.rmtPointerResets);
    // Preloaded: one PSRAM buffer per run. Progressive: the loader's block table and its blocks.
    if (progressiveExpected) {
        TEST_ASSERT_TRUE_MESSAGE(s.heapAllocs > fskRuns + 1, "the loader did not run");
    } else {
        TEST_ASSERT_EQUAL_UINT_MESSAGE(fskRuns, s.heapAllocs, "a run was not preloaded");
    }
}

}  // namespace

void setUp() { resetAll(); }
void tearDown() {}

// International Karate: 106 records and 107 short FSK runs (preloaded), every byte and portion in order.
void test_real_international_karate_whole_line()
{
    playWholeTape("international_karate.cas", false);
}

// PAL Alien Ambush: records and FSK runs alternate (preloaded).
void test_real_pal_alien_ambush_whole_line()
{
    playWholeTape("PAL_ALIEN_AMBUSH_TURBO_SOFTWARE.cas", false);
}

// Turbo Software Missile Command: one FSK run whose first chunk is 65 532 bytes, loaded progressively by
// the owner's loader task (a host thread here) while the RMT plays it.
void test_real_missile_command_progressive_whole_line()
{
    playWholeTape("turbo_software_missile_command.cas", true);
}

namespace {

// A tape with one FUJI chunk and one fsk run of `values` values of 10 (1 ms each).
Bytes fskTape(unsigned values)
{
    Bytes f = {'F', 'U', 'J', 'I', 0, 0, 0, 0};
    const unsigned len = values * 2;
    f.insert(f.end(), {'f', 's', 'k', ' ', static_cast<uint8_t>(len & 0xff), static_cast<uint8_t>(len >> 8), 0, 0});
    for (unsigned i = 0; i < values; ++i) {
        f.push_back(10);
        f.push_back(0);
    }
    return f;
}

}  // namespace

// MOTOR drops while the run plays: the encoder stops, the pin goes back to the UART first (D8: while the
// channel is still enabled), the classic ESP32 abort writes an EOF into RMT word 0 and resets the read
// pointer, and the run reports motor_dropped (FujiTapeLoop then replays it).
void test_motor_drop_mid_run_releases_the_pin_before_the_channel()
{
    const Bytes f = fskTape(4000);
    MemoryTapeSource src({f.data(), f.size()});
    EspFskEngine engine;
    engine.attach(&src, &motorDropped, nullptr);
    g_batch = 64;
    g_dropAtWait = 3;
    RMTMEM[0] = 0xdeadbeef;

    const rb::fujinet::FskRunOutcome out = engine.play(8, 8000);

    TEST_ASSERT_FALSE(out.ok);
    TEST_ASSERT_EQUAL_UINT8(kStatusMotorDropped, engine.lastStatus());
    TEST_ASSERT_EQUAL_UINT32(0, RMTMEM[0]);
    TEST_ASSERT_EQUAL_UINT(1, fake_idf::state().rmtPointerResets);
    TEST_ASSERT_TRUE(g_ownerAtDrain == fake_idf::Owner::Uart);
    TEST_ASSERT_TRUE(g_enabledAtDrain);
    TEST_ASSERT_TRUE(g_line.size() < 4000);  // cut short
    assertPinBackOnUart();
}

// The abort touches RMT memory only if the channel owns all 8 blocks; otherwise the stop flag lets the
// transmission drain, and the run still reports motor_dropped.
void test_motor_drop_without_all_blocks_skips_the_memory_abort()
{
    const Bytes f = fskTape(4000);
    MemoryTapeSource src({f.data(), f.size()});
    EspFskEngine engine;
    engine.attach(&src, &motorDropped, nullptr);
    fake_idf::state().rmtMemBlocks = 1;
    g_batch = 64;
    g_dropAtWait = 3;
    RMTMEM[0] = 0xdeadbeef;

    const rb::fujinet::FskRunOutcome out = engine.play(8, 8000);

    TEST_ASSERT_FALSE(out.ok);
    TEST_ASSERT_EQUAL_UINT8(kStatusMotorDropped, engine.lastStatus());
    TEST_ASSERT_EQUAL_UINT32(0xdeadbeef, RMTMEM[0]);
    TEST_ASSERT_EQUAL_UINT(0, fake_idf::state().rmtPointerResets);
    assertPinBackOnUart();
}

// No PSRAM for the preload: allocation_failed, no RMT channel, nothing on the line.
void test_no_memory_reports_allocation_failed()
{
    const Bytes f = fskTape(100);
    MemoryTapeSource src({f.data(), f.size()});
    EspFskEngine engine;
    engine.attach(&src, &motorDropped, nullptr);
    fake_idf::state().failHeap = true;

    const rb::fujinet::FskRunOutcome out = engine.play(8, 200);

    TEST_ASSERT_FALSE(out.ok);
    TEST_ASSERT_EQUAL_UINT8(kStatusAllocationFailed, engine.lastStatus());
    TEST_ASSERT_EQUAL_UINT(0, fake_idf::state().rmtNewChannels);
    TEST_ASSERT_TRUE(g_line.empty());
}

// A run that plays to its end: ok, next offset past it, waveform 100 values x 1 ms, pin back on the UART.
void test_short_run_plays_and_reports_its_length()
{
    const Bytes f = fskTape(100);
    MemoryTapeSource src({f.data(), f.size()});
    EspFskEngine engine;
    engine.attach(&src, &motorDropped, nullptr);

    const rb::fujinet::FskRunOutcome out = engine.play(8, 200);

    TEST_ASSERT_TRUE(out.ok);
    TEST_ASSERT_EQUAL_UINT32(f.size(), out.nextOffset);
    TEST_ASSERT_EQUAL_UINT32(100, out.waveformMs);
    TEST_ASSERT_EQUAL_UINT32(100, g_line.size());
    TEST_ASSERT_FALSE(g_line[0].portion.level);  // index 0: space
    TEST_ASSERT_TRUE(g_line[1].portion.level);
    TEST_ASSERT_EQUAL_UINT32(1000, g_line[0].portion.ticks);
    TEST_ASSERT_EQUAL_INT(1, fake_idf::state().gpioLevel);  // MARK while the RMT takes the pin
    assertPinBackOnUart();
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_short_run_plays_and_reports_its_length);
    RUN_TEST(test_motor_drop_mid_run_releases_the_pin_before_the_channel);
    RUN_TEST(test_motor_drop_without_all_blocks_skips_the_memory_abort);
    RUN_TEST(test_no_memory_reports_allocation_failed);
    RUN_TEST(test_real_international_karate_whole_line);
    RUN_TEST(test_real_pal_alien_ambush_whole_line);
    RUN_TEST(test_real_missile_command_progressive_whole_line);
    return UNITY_END();
}
