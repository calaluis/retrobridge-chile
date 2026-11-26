// Review of PR #122, H1: the FujiNet engine on the topology menu-sio uses — DATA IN shared with the disk on
// UART2 (EspDataIn::attachShared, as CassetteService::shareDataIn does), lent by the SIO task through the
// real DataInHandover and the firmware's ports (main/cassette_ports.hpp), the end published to the real
// TapeProgress. Under it the owner's engine as it is (cassetteFSK.cpp, cassetteFSKLoader.cpp) over
// fake_idf, with its TX model (ring, FIFO, shift register, time) and GPIO matrix.
//
// What it proves: the engine is the one chosen on this topology; it plays only while the cassette holds
// DATA IN; a release during an FSK run stops the owner's run through its own motor_dropped callback, puts
// GPIO21 back on UART2 TX and is confirmed Disk well within the arbiter's 600 ms; the run plays again whole
// once the line comes back; the end reaches TapeProgress. What it cannot prove: the real driver, the RMT
// and the 65XE (T9b). No gate changes: kTapeOutput stays false in main/menu_sio.cpp.
#include <cstdint>
#include <new>
#include <vector>

#include "cassette_ports.hpp"
#include "driver/rmt_encoder.h"
#include "driver/uart.h"
#include "fake_idf.hpp"
#include "rb/board/board_profile.hpp"
#include "rb/board/fujinet_atari_v1x.hpp"
#include "rb/cassette/tape_source.hpp"
#include "rb/esp/esp_data_in.hpp"
#include "rb/esp/esp_sio_uart.hpp"
#include "rb/fujinet/esp_fsk_engine.hpp"
#include "rb/fujinet/fuji_service_rules.hpp"
#include "rb/fujinet/fuji_tape_runner.hpp"
#include "rb/menu/data_in_arbiter.hpp"
#include "rb/menu/data_in_handover.hpp"
#include "rb/menu/tape_progress.hpp"
#include "unity.h"

using rb::cassette::MemoryTapeSource;
using rb::esp::EspDataIn;
using rb::fujinet::FujiStepKind;
using rb::fujinet::FujiTapeRunner;
using rb::host::DataInHandover;
using rb::host::HandoverAck;
using rb::host::HandoverRequest;
using Bytes = std::vector<uint8_t>;

void setUp() {}
void tearDown() {}

namespace {

constexpr int kDataIn = 21;
constexpr int kDataOut = 33;
constexpr unsigned kRunValues = 400;  // an FSK run long enough to be cut: 400 portions

class Motor final : public rb::cassette::MotorIn {
public:
    bool level = true;
    bool on() const override { return level; }
};

// The cassette task's millisecond: the line runs for 1 ms of model time.
class ModelPause final : public rb::fujinet::Pause {
public:
    void waitOneMs() override { fake_idf::tx_advance(1000); }
};

std::vector<bool> g_levels;     // every RMT portion's level, in order
unsigned g_waits = 0;           // rmt_tx_wait_all_done() calls in the current run
unsigned g_releaseAtWait = 0;   // 0: never; else the SIO task asks DATA IN back at this wait
DataInHandover* g_handover = nullptr;
uint32_t g_releaseSeq = 0;
int64_t g_releaseAtUs = 0;

void recordSymbols(const void* symbols, std::size_t count)
{
    const auto* s = static_cast<const rmt_symbol_word_t*>(symbols);
    for (std::size_t i = 0; i < count; ++i) {
        if (s[i].duration0 > 0) {
            g_levels.push_back(s[i].level0 != 0);
        }
        if (s[i].duration1 > 0) {
            g_levels.push_back(s[i].level1 != 0);
        }
    }
}

void onWait()
{
    ++g_waits;
    if (g_releaseAtWait != 0 && g_waits == g_releaseAtWait && g_handover != nullptr) {
        g_releaseSeq = g_handover->request(HandoverRequest::Release);
        g_releaseAtUs = fake_idf::state().nowUs;
    }
    fake_idf::play_symbols(16);  // a few symbols per 100 ms wait: the run is still playing at the release
}

EspDataIn* g_dataIn = nullptr;
void flushOutput()
{
    while (g_dataIn != nullptr && !g_dataIn->drained()) {
        fake_idf::tx_advance(1000);
    }
}

FujiTapeRunner* g_runner = nullptr;
bool motorDropped(void*) { return g_runner->stopNow(); }  // CassetteService::fujiMotorDropped

void chunk(Bytes& f, const char* type, uint16_t aux, const Bytes& payload)
{
    f.insert(f.end(), type, type + 4);
    f.push_back(static_cast<uint8_t>(payload.size() & 0xff));
    f.push_back(static_cast<uint8_t>(payload.size() >> 8));
    f.push_back(static_cast<uint8_t>(aux & 0xff));
    f.push_back(static_cast<uint8_t>(aux >> 8));
    f.insert(f.end(), payload.begin(), payload.end());
}

// A record, an FSK run of kRunValues portions of 1 ms (10 units of 100 us), a record.
Bytes tape()
{
    Bytes f;
    chunk(f, "FUJI", 0, {});
    chunk(f, "data", 0, {1, 2, 3});
    Bytes run;
    for (unsigned i = 0; i < kRunValues; ++i) {
        run.push_back(10);
        run.push_back(0);
    }
    chunk(f, "fsk ", 0, run);
    chunk(f, "data", 0, {9});
    return f;
}

rb::board::CassetteOutputAssignment granted()
{
    rb::board::BoardProfile p = rb::board::fujinet_atari_v1x_profile();
    p.sioGateApproved = true;
    p.motorGateApproved = true;
    return rb::board::validate_for_cassette_output(p).value();
}

alignas(EspDataIn) unsigned char g_storage[sizeof(EspDataIn)];

// The disk installed UART2 with its shared TX ring and routed DATA IN to its TX (menu-sio, kTapeOutput on);
// the cassette attaches in shared mode, as CassetteService::shareDataIn does.
EspDataIn& sharedDataIn()
{
    fake_idf::reset();
    auto& s = fake_idf::state();
    s.txModel = true;
    s.uartBaud = 19200;
    TEST_ASSERT_EQUAL(ESP_OK, uart_driver_install(UART_NUM_2, 1024, rb::esp::EspSioUart::kSharedTxRing, 0, nullptr, 0));
    TEST_ASSERT_EQUAL(ESP_OK, uart_set_pin(UART_NUM_2, kDataIn, kDataOut, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    s.onRmtWait = &onWait;
    s.onSymbols = &recordSymbols;
    g_dataIn = new (g_storage) EspDataIn();
    TEST_ASSERT_TRUE(g_dataIn->attachShared(granted(), static_cast<std::size_t>(rb::esp::EspSioUart::kSharedTxRing)));
    g_levels.clear();
    g_waits = 0;
    g_releaseAtWait = 0;
    rb::fujinet::setDataInPin(kDataIn);
    rb::fujinet::setFlushOutput(&flushOutput);
    return *g_dataIn;
}

struct Chain {
    Bytes file = tape();
    MemoryTapeSource src{{file.data(), file.size()}};
    EspDataIn& dataIn = sharedDataIn();
    Motor motor;
    ModelPause pause;
    rb::fujinet::EspFskEngine engine;
    FujiTapeRunner runner{dataIn, motor, engine, pause};
    DataInHandover handover;
    rb::menusio::HandoverLender lender{handover};
    rb::host::TapeProgress progress;
    rb::menusio::ProgressSink sink{progress};

    Chain()
    {
        g_handover = &handover;
        g_runner = &runner;
        TEST_ASSERT_TRUE(engine.ready());
        // CassetteService::start(): the engine is chosen on this topology, and keeps the handover.
        TEST_ASSERT_TRUE(rb::fujinet::fujiEngineChosen(true, dataIn.shared(), engine.ready()));
        runner.lend(lender, dataIn);
        runner.report(sink);
        runner.follow(&src, 1);
        engine.attach(&src, &motorDropped, nullptr);
    }
    ~Chain()
    {
        g_handover = nullptr;
        g_runner = nullptr;
    }

    // The SIO task asks; the cassette task answers on its turns. Returns the ack.
    HandoverAck ask(HandoverRequest kind)
    {
        const uint32_t seq = handover.request(kind);
        HandoverAck ack = HandoverAck::Refused;
        for (int turn = 0; turn < 50 && !handover.ackFor(seq, ack); ++turn) {
            runner.tick();
            fake_idf::tx_advance(2000);
        }
        TEST_ASSERT_TRUE(handover.ackFor(seq, ack));
        return ack;
    }
};

void assertPinOnUart()
{
    const auto& s = fake_idf::state();
    TEST_ASSERT_EQUAL_UINT32(fake_idf::kU2TxdOutIdx, s.outSel);
    TEST_ASSERT_TRUE(s.outEnabled);
    TEST_ASSERT_FALSE(s.rmtChannel);
}

}  // namespace

// The whole tape on the shared line: nothing moves until the SIO task lends DATA IN; then the records go
// out on UART2, the run on the owner's RMT channel and back, and the end reaches TapeProgress.
void test_the_engine_plays_the_tape_on_the_shared_line()
{
    Chain c;
    TEST_ASSERT_FALSE(c.runner.tick());  // the disk has the line
    TEST_ASSERT_EQUAL_UINT64(0, fake_idf::state().txBytesOut);

    TEST_ASSERT_TRUE(HandoverAck::Tape == c.ask(HandoverRequest::Take));
    TEST_ASSERT_TRUE(c.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Data, c.runner.last().kind);
    TEST_ASSERT_EQUAL_UINT64(3, fake_idf::state().txBytesOut);
    TEST_ASSERT_TRUE(c.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Fsk, c.runner.last().kind);
    TEST_ASSERT_EQUAL(kRunValues, g_levels.size());
    for (unsigned i = 0; i < kRunValues; ++i) {
        TEST_ASSERT_EQUAL(i % 2 == 1, g_levels[i]);  // even index = space (low), odd = mark (high)
    }
    assertPinOnUart();
    TEST_ASSERT_TRUE(c.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Data, c.runner.last().kind);
    TEST_ASSERT_EQUAL_UINT64(4, fake_idf::state().txBytesOut);
    TEST_ASSERT_FALSE(c.progress.ended());
    TEST_ASSERT_TRUE(c.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::End, c.runner.last().kind);
    TEST_ASSERT_TRUE(c.progress.ended());
    TEST_ASSERT_TRUE(rb::menusio::tapeEnded(c.progress));

    TEST_ASSERT_TRUE(HandoverAck::Disk == c.ask(HandoverRequest::Release));
    assertPinOnUart();
    TEST_ASSERT_TRUE(fake_idf::tx_empty());
}

// The disk asks the line back in the middle of the FSK run: the owner's run stops through its motor_dropped
// callback (FujiTapeRunner::stopNow), GPIO21 goes back to UART2 TX and the release is confirmed Disk within
// the arbiter's timeout. With the line back the run plays again, whole.
void test_a_release_in_an_fsk_run_gives_the_line_back_and_the_run_replays_whole()
{
    Chain c;
    TEST_ASSERT_TRUE(HandoverAck::Tape == c.ask(HandoverRequest::Take));
    TEST_ASSERT_TRUE(c.runner.tick());  // the first record
    g_releaseAtWait = 3;
    TEST_ASSERT_TRUE(c.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Replay, c.runner.last().kind);
    TEST_ASSERT_TRUE(g_releaseSeq != 0);
    TEST_ASSERT_TRUE(g_levels.size() < kRunValues);  // cut: not played to its end
    HandoverAck ack = HandoverAck::Refused;
    TEST_ASSERT_TRUE(c.handover.ackFor(g_releaseSeq, ack));  // answered in the same turn as the step
    TEST_ASSERT_TRUE(HandoverAck::Disk == ack);
    TEST_ASSERT_TRUE(fake_idf::state().nowUs - g_releaseAtUs < static_cast<int64_t>(rb::host::DataInArbiter::kReleaseTimeoutUs));
    assertPinOnUart();
    TEST_ASSERT_FALSE(c.runner.tick());  // without the line the tape does not move
    TEST_ASSERT_EQUAL_UINT64(3, fake_idf::state().txBytesOut);

    g_releaseAtWait = 0;
    g_waits = 0;
    g_levels.clear();
    TEST_ASSERT_TRUE(HandoverAck::Tape == c.ask(HandoverRequest::Take));
    TEST_ASSERT_TRUE(c.runner.tick());
    TEST_ASSERT_EQUAL(FujiStepKind::Fsk, c.runner.last().kind);
    TEST_ASSERT_EQUAL(kRunValues, g_levels.size());
    TEST_ASSERT_FALSE(g_levels[0]);  // from its first portion
    assertPinOnUart();
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_the_engine_plays_the_tape_on_the_shared_line);
    RUN_TEST(test_a_release_in_an_fsk_run_gives_the_line_back_and_the_run_replays_whole);
    return UNITY_END();
}
