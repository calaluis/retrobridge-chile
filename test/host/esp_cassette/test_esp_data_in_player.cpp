// Review H1 of PR #121 (cassette-cas-paridad D1): CassettePlayer driving the real EspDataIn against
// fake_idf, in CassetteTick's order (player.tick(), then pump()). The RMT can run dry after the player
// has pushed the end of a run and entered FskDrain; pump() then cuts the run and drops what came late.
// That run must be played again from its gap, as the owner's beta does on CassetteFSKStatus::underrun
// (FujiNet PR #1773; PR #1756 cassette.cpp), never taken as played. A run that did play to its end is
// never played twice: normal end, MOTOR off, rewind and output change are checked both ways.
// What it cannot prove: RMT timing, electrical levels, or that the real IDF behaves as fake_idf.

#include <cstdint>
#include <new>
#include <vector>

#include "fakes/idf/fake_idf.hpp"
#include "rb/board/board_profile.hpp"
#include "rb/board/fujinet_atari_v1x.hpp"
#include "rb/cassette/cas_index.hpp"
#include "rb/cassette/cassette_player.hpp"
#include "rb/cassette/discard_outputs.hpp"
#include "rb/cassette/fsk_loader.hpp"
#include "rb/cassette/tape_source.hpp"
#include "rb/esp/esp_data_in.hpp"
#include "unity.h"

using rb::cassette::CasIndex;
using rb::cassette::CasIndexResult;
using rb::cassette::CasSegment;
using rb::cassette::CassettePlayer;
using rb::cassette::CasStatus;
using rb::cassette::DiscardByteOut;
using rb::cassette::DiscardWaveOut;
using rb::cassette::FskLoader;
using rb::cassette::LoaderStep;
using rb::cassette::MemoryTapeSource;
using rb::cassette::MotorIn;
using rb::cassette::PlayerPhase;
using rb::core::Span;
using rb::esp::EspDataIn;

namespace {

using Bytes = std::vector<uint8_t>;

// A test-only copy with both gates signed: nothing here approves H4 or M3a.
rb::board::CassetteOutputAssignment granted()
{
    rb::board::BoardProfile p = rb::board::fujinet_atari_v1x_profile();
    p.sioGateApproved = true;
    p.motorGateApproved = true;
    return rb::board::validate_for_cassette_output(p).value();
}

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

Bytes repeat(std::size_t count, uint16_t units)
{
    Bytes out;
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(static_cast<uint8_t>(units & 0xFF));
        out.push_back(static_cast<uint8_t>(units >> 8));
    }
    return out;
}

class FakeMotor final : public MotorIn {
public:
    bool motor = true;
    bool on() const override { return motor; }
};

// One FSK run of 9000 values (first chunk 18000 bytes: progressive), then a one-byte record.
// The feed holds 4096 portions, so the run needs three fills of it.
constexpr uint32_t kRunValues = 9000;

alignas(EspDataIn) unsigned char g_storage[sizeof(EspDataIn)];
CasSegment g_table[8];
uint8_t g_bank[64 * 1024];

struct Rig {
    Bytes file = chunk("FUJI", 0, {}) + chunk("fsk ", 0, repeat(kRunValues, 300)) + chunk("data", 0, {7});
    MemoryTapeSource source{Span<const uint8_t>(file.data(), file.size())};
    FskLoader loader;
    CassettePlayer player{loader};
    FakeMotor motor;
    EspDataIn* dataIn = nullptr;
    DiscardByteOut discardBytes;
    DiscardWaveOut discardWave;
    bool discard = false;  // the player plays into discard (after changeOutputs)
    uint64_t now = 0;

    Rig()
    {
        dataIn = new (g_storage) EspDataIn();
        TEST_ASSERT_TRUE(dataIn->configure(granted()));
        const CasIndexResult index = CasIndex::build(source, Span<CasSegment>(g_table, 8));
        TEST_ASSERT_EQUAL(static_cast<int>(CasStatus::Ok), static_cast<int>(index.status));
        loader.bind(Span<uint8_t>(g_bank, sizeof g_bank));
        player.load(source, Span<const CasSegment>(g_table, index.count), *dataIn);
    }
    // CassetteTick::tick: the player first, then the line's pump.
    void tick()
    {
        ++now;
        if (discard) {
            player.tick(now, motor, discardBytes, discardWave);
        } else {
            player.tick(now, motor, *dataIn, *dataIn);
        }
        dataIn->pump();
    }
    void loadAll()
    {
        while (loader.step() == LoaderStep::Progress) {
        }
    }
    // Gate open, the transmission started, then two fills of the feed played by the RMT without
    // ending it: 8192 portions pushed, 808 left, the player still in FskPlay.
    void playToTheLastFill()
    {
        tick();  // the gap (0): the player starts the run's loader and waits for its gate
        for (int i = 0; i < 7; ++i) {
            (void)loader.step();  // header + 6 blocks: 1536 values, the gate opens
        }
        tick();
        TEST_ASSERT_TRUE(dataIn->fskActive());
        loadAll();
        tick();                                                     // the feed fills: 4096
        TEST_ASSERT_EQUAL_size_t(2048, fake_idf::play_symbols(2048));  // played, still transmitting
        tick();                                                     // 8192 pushed
        TEST_ASSERT_EQUAL_size_t(2048, fake_idf::play_symbols(2048));
        TEST_ASSERT_EQUAL(static_cast<int>(PlayerPhase::FskPlay), static_cast<int>(player.phase()));
    }
    // H1: the RMT runs dry and ends the transmission; in the same cassette tick the player pushes the
    // last 808 portions and enters FskDrain; pump() then cuts the run and drops them.
    void cutAfterTheLastPush()
    {
        playToTheLastFill();
        (void)fake_idf::play_all();  // nothing queued: the RMT ends the transmission
        tick();
        TEST_ASSERT_EQUAL(static_cast<int>(PlayerPhase::FskDrain), static_cast<int>(player.phase()));
        TEST_ASSERT_EQUAL_UINT32(1, dataIn->underruns());  // cut, the late portions dropped
        TEST_ASSERT_TRUE(dataIn->idle());
    }
    // The run plays to its end: the last portions are pushed while the RMT still transmits.
    void playToTheEnd()
    {
        const uint32_t cutsBefore = dataIn->underruns();  // a rewind may follow an earlier cut
        playToTheLastFill();
        tick();  // the last 808 portions, the player in FskDrain
        TEST_ASSERT_EQUAL(static_cast<int>(PlayerPhase::FskDrain), static_cast<int>(player.phase()));
        TEST_ASSERT_TRUE(fake_idf::play_all() > 0);  // the rest plays, then the RMT ends
        tick();                                       // pump gives the pin back to the UART
        TEST_ASSERT_TRUE(dataIn->idle());
        TEST_ASSERT_EQUAL_UINT32(cutsBefore, dataIn->underruns());
    }
    // Plays every later run whole, until the tape's record.
    void finish()
    {
        for (int i = 0; i < 1000 && player.segment() == 0; ++i) {
            loadAll();
            tick();
            if (player.phase() == PlayerPhase::FskDrain) {
                (void)fake_idf::play_all();  // everything pushed: the RMT plays it and ends
            } else {
                (void)fake_idf::play_symbols(2048);  // keeps up without ending the transmission
            }
        }
    }
};

}  // namespace

void setUp() { fake_idf::reset(); }
void tearDown() {}

// H1: a run cut after the player entered FskDrain is played again from its gap, not taken as played.
static void test_cut_after_the_last_push_replays_the_run(void)
{
    Rig rig;
    rig.cutAfterTheLastPush();
    rig.tick();
    TEST_ASSERT_EQUAL_UINT32(0, rig.player.segment());
    TEST_ASSERT_EQUAL(static_cast<int>(PlayerPhase::Gap), static_cast<int>(rig.player.phase()));
    TEST_ASSERT_EQUAL_UINT32(1, rig.player.fskUnderruns());
    rig.finish();
    TEST_ASSERT_EQUAL_UINT32(1, rig.player.segment());
    TEST_ASSERT_EQUAL_UINT32(1, rig.player.fskUnderruns());
}

// A run that played to its end goes on to the next segment, once.
static void test_normal_end_is_not_replayed(void)
{
    Rig rig;
    rig.playToTheEnd();
    rig.tick();
    TEST_ASSERT_EQUAL_UINT32(1, rig.player.segment());
    TEST_ASSERT_EQUAL_UINT32(0, rig.player.fskUnderruns());
    TEST_ASSERT_EQUAL_UINT32(0, rig.player.fskRestarts());
}

// MOTOR off right after a cut in FskDrain: the run is played again when MOTOR comes back.
static void test_motor_off_after_a_cut_replays_the_run(void)
{
    Rig rig;
    rig.cutAfterTheLastPush();
    rig.motor.motor = false;
    rig.tick();
    TEST_ASSERT_EQUAL_UINT32(0, rig.player.segment());
    TEST_ASSERT_EQUAL(static_cast<int>(PlayerPhase::Gap), static_cast<int>(rig.player.phase()));
    TEST_ASSERT_EQUAL_UINT32(1, rig.player.fskUnderruns());
}

// MOTOR off after a run that played to its end: nothing is replayed.
static void test_motor_off_after_a_normal_end_does_not_replay(void)
{
    Rig rig;
    rig.playToTheEnd();
    rig.motor.motor = false;
    rig.tick();
    TEST_ASSERT_EQUAL_UINT32(1, rig.player.segment());
    TEST_ASSERT_EQUAL_UINT32(0, rig.player.fskRestarts());
}

// Rewind after a cut: the tape starts again, and the old cut does not count against the next run.
static void test_rewind_after_a_cut_then_a_normal_run(void)
{
    Rig rig;
    rig.cutAfterTheLastPush();
    rig.player.rewind(*rig.dataIn);
    TEST_ASSERT_EQUAL_UINT32(0, rig.player.segment());
    TEST_ASSERT_EQUAL_UINT32(0, rig.player.fskUnderruns());
    rig.tick();  // the stopped producer, then the run starts again
    rig.playToTheEnd();
    rig.tick();
    TEST_ASSERT_EQUAL_UINT32(1, rig.player.segment());
    TEST_ASSERT_EQUAL_UINT32(0, rig.player.fskUnderruns());
}

// Output change after a cut: the run is played again, whole, on the new output.
static void test_output_change_after_a_cut_replays_the_run(void)
{
    Rig rig;
    rig.cutAfterTheLastPush();
    rig.player.changeOutputs(*rig.dataIn, true);
    rig.discard = true;
    TEST_ASSERT_EQUAL_UINT32(0, rig.player.segment());
    TEST_ASSERT_EQUAL(static_cast<int>(PlayerPhase::Gap), static_cast<int>(rig.player.phase()));
    rig.finish();
    TEST_ASSERT_EQUAL_UINT32(1, rig.player.segment());
    TEST_ASSERT_EQUAL_UINT32(kRunValues, rig.discardWave.portions());
}

// Output change after a normal end: the run is not played again.
static void test_output_change_after_a_normal_end_does_not_replay(void)
{
    Rig rig;
    rig.playToTheEnd();
    rig.player.changeOutputs(*rig.dataIn, true);
    rig.discard = true;
    TEST_ASSERT_EQUAL_UINT32(1, rig.player.segment());
    TEST_ASSERT_EQUAL_UINT32(0, rig.player.fskRestarts());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_cut_after_the_last_push_replays_the_run);
    RUN_TEST(test_normal_end_is_not_replayed);
    RUN_TEST(test_motor_off_after_a_cut_replays_the_run);
    RUN_TEST(test_motor_off_after_a_normal_end_does_not_replay);
    RUN_TEST(test_rewind_after_a_cut_then_a_normal_run);
    RUN_TEST(test_output_change_after_a_cut_replays_the_run);
    RUN_TEST(test_output_change_after_a_normal_end_does_not_replay);
    return UNITY_END();
}
