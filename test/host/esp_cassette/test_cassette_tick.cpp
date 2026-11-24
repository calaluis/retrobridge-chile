// menu-cinta T9a.7 (plan requirements 1 and 2): the cassette task's tick, CassetteTick, end to end on the
// host. The real CassettePlayer, PlayerOutputs, DataInHandover, HandoverTapeLine and DataInArbiter, with the
// firmware's adapters (main/cassette_ports.hpp). Requirement 1 with live outputs the test reads; requirement
// 2 (ended() -> TapeProgress -> arbiter) with the real EspDataIn in shared mode on fake_idf's TX model.

#include <cstdint>
#include <new>
#include <vector>

#include "cassette_ports.hpp"
#include "driver/uart.h"
#include "fakes/idf/fake_idf.hpp"
#include "rb/board/board_profile.hpp"
#include "rb/board/fujinet_atari_v1x.hpp"
#include "rb/cassette/cas_index.hpp"
#include "rb/cassette/cassette_tick.hpp"
#include "rb/cassette/discard_outputs.hpp"
#include "rb/cassette/fsk_loader.hpp"
#include "rb/esp/esp_data_in.hpp"
#include "rb/menu/data_in_arbiter.hpp"
#include "rb/menu/data_in_handover.hpp"
#include "rb/menu/tape_progress.hpp"
#include "unity.h"

using rb::cassette::ByteOut;
using rb::cassette::CasIndex;
using rb::cassette::CasIndexResult;
using rb::cassette::CasSegment;
using rb::cassette::CassettePlayer;
using rb::cassette::CassetteTick;
using rb::cassette::FskLoader;
using rb::cassette::FskPortion;
using rb::cassette::LiveLine;
using rb::cassette::MemoryTapeSource;
using rb::cassette::MotorIn;
using rb::cassette::PlayerOutputs;
using rb::cassette::WaveOut;
using rb::core::Span;
using rb::host::DataInArbiter;
using rb::host::DataInChange;
using rb::host::DataInHandover;
using rb::host::DataInInputs;
using rb::host::DataInOwner;
using rb::host::HandoverTapeLine;
using rb::host::LineResult;

namespace {

using Bytes = std::vector<uint8_t>;

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

Bytes fuji() { return chunk("FUJI", 0, {}); }

Bytes units(std::initializer_list<uint16_t> list)
{
    Bytes out;
    for (uint16_t v : list) {
        out.push_back(static_cast<uint8_t>(v & 0xFF));
        out.push_back(static_cast<uint8_t>(v >> 8));
    }
    return out;
}

// A record with no gap (aux 0), then a short FSK run (5 portions), then a record.
Bytes recordAndRun() { return fuji() + chunk("data", 0, {1, 2, 3}) + chunk("fsk ", 0, units({4, 5, 6, 7, 8})) + chunk("data", 0, {9}); }

class FakeMotor final : public MotorIn {
public:
    bool motor = true;
    bool on() const override { return motor; }
};

// The live outputs the test reads (EspDataIn on the ESP32).
class LiveBytes final : public ByteOut {
public:
    bool setBaud(uint32_t) override { return true; }
    bool send(Span<const uint8_t> bytes) override
    {
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            sent.push_back(bytes[i]);
        }
        return true;
    }
    bool drained() const override { return true; }
    Bytes sent;
};

class LiveWave final : public WaveOut {
public:
    bool push(const FskPortion& portion) override
    {
        ticks.push_back(portion.ticks);
        return true;
    }
    bool idle() const override { return true; }  // plays at once
    void stop() override { ++stops; }
    std::vector<uint32_t> ticks;
    unsigned stops = 0;
};

// The hardware under the live outputs: free or not, and how giveBack() answers.
class FakeLine final : public LiveLine {
public:
    bool hardwareFree() const noexcept override { return free; }
    GiveBack giveBack() noexcept override
    {
        ++giveBacks;
        return answer;
    }
    void pump() noexcept override { ++pumps; }
    bool free = true;
    GiveBack answer = GiveBack::Uart;
    unsigned giveBacks = 0;
    unsigned pumps = 0;
};

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

CasSegment g_table[64];
uint8_t g_bank[1u << 16];

// The tape, loaded in a real player.
struct Tape {
    Bytes file;
    MemoryTapeSource source;
    FskLoader loader;
    CassettePlayer player{loader};

    explicit Tape(const Bytes& bytes, WaveOut& wave) : file(bytes), source(Span<const uint8_t>(file.data(), file.size()))
    {
        const CasIndexResult index = CasIndex::build(source, Span<CasSegment>(g_table, 64));
        TEST_ASSERT_EQUAL(0, static_cast<int>(index.status));
        loader.bind(Span<uint8_t>(g_bank, sizeof g_bank));
        player.load(source, Span<const CasSegment>(g_table, index.count), wave);
    }
    void produce()
    {
        for (int i = 0; i < 100000 && loader.step() == rb::cassette::LoaderStep::Progress; ++i) {
        }
    }
};

DataInInputs inputs(bool motorOn)
{
    DataInInputs in;
    in.gates = true;
    in.motorOn = motorOn;
    in.tapeReady = true;
    return in;
}

constexpr uint64_t kTickUs = 2000;

// Requirement 1, with the outputs the test reads.
struct Deck {
    rb::cassette::DiscardByteOut discardBytes;
    rb::cassette::DiscardWaveOut discardWave;
    LiveBytes liveBytes;
    LiveWave liveWave;
    PlayerOutputs outputs{discardBytes, discardWave};
    FakeMotor motor;
    Tape tape;
    CassetteTick tick{tape.player, outputs, motor};
    FakeLine line;
    DataInHandover handover;
    rb::menusio::HandoverLender lender{handover};
    rb::host::TapeProgress progress;
    rb::menusio::ProgressSink sink{progress};
    FakeDisk disk;
    HandoverTapeLine tapeLine{handover};
    DataInArbiter arbiter{disk, tapeLine};
    uint64_t now = 0;

    explicit Deck(const Bytes& file, bool live = true) : tape(file, discardWave)
    {
        if (live) {
            outputs.setLive(liveBytes, liveWave);
            tick.lend(lender, line);
        }
        tick.report(sink);
    }

    DataInChange step()
    {
        tape.produce();
        now += kTickUs;
        tick.tick(now);  // the cassette task
        DataInInputs in = inputs(motor.motor);
        in.tapeEnded = rb::menusio::tapeEnded(progress);
        const DataInChange change = arbiter.step(in, now);  // the SIO task
        TEST_ASSERT_FALSE_MESSAGE(disk.serving && outputs.live(), "two transmitters on GPIO21");
        return change;
    }
};

// The same tape played straight into live outputs from the first tick: what the Atari must get.
struct Reference {
    LiveBytes bytes;
    LiveWave wave;
    FakeMotor motor;
    Tape tape;

    explicit Reference(const Bytes& file) : tape(file, wave)
    {
        for (int i = 0; i < 20000 && !tape.player.atEnd(); ++i) {
            tape.produce();
            tape.player.tick(static_cast<uint64_t>(i + 1) * kTickUs, motor, bytes, wave);
        }
        TEST_ASSERT_TRUE(tape.player.atEnd());
    }
};

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// Requirement 1 (review of PR #102): MOTOR on and the take pending for many ticks (the cassette's hardware
// not free yet): zero bytes and zero FSK portions go to discard and nothing goes live. Once Tape is
// confirmed, the whole tape, the record with no gap and the short FSK run included, goes out live.
void test_a_pending_take_loses_nothing_and_the_whole_tape_goes_live()
{
    const Reference ref(recordAndRun());
    Deck d(recordAndRun());
    d.line.free = false;
    int takes = 0;  // each take expires after kTakeTimeoutUs and is asked again after kRetryUs
    for (int i = 0; i < 200; ++i) {
        takes += d.step() == DataInChange::TakeRequested ? 1 : 0;
        TEST_ASSERT_TRUE(d.arbiter.owner() != DataInOwner::Tape);
    }
    TEST_ASSERT_TRUE(takes >= 2);
    TEST_ASSERT_EQUAL_UINT32(0, d.discardBytes.bytes());
    TEST_ASSERT_EQUAL_UINT32(0, d.discardWave.portions());
    TEST_ASSERT_EQUAL_UINT32(0, d.liveBytes.sent.size());
    TEST_ASSERT_EQUAL_UINT32(0, d.liveWave.ticks.size());
    TEST_ASSERT_EQUAL_UINT32(0, d.tape.player.segment());
    d.line.free = true;
    bool toTape = false;
    for (int i = 0; i < 20000 && !d.tape.player.atEnd(); ++i) {
        toTape = d.step() == DataInChange::ToTape || toTape;
    }
    TEST_ASSERT_TRUE(toTape);
    TEST_ASSERT_TRUE(d.line.pumps > 0);  // the live line is pumped every tick
    TEST_ASSERT_TRUE(d.tape.player.atEnd());
    TEST_ASSERT_TRUE(d.liveBytes.sent == ref.bytes.sent);
    TEST_ASSERT_TRUE(d.liveWave.ticks == ref.wave.ticks);
    TEST_ASSERT_FALSE(ref.wave.ticks.empty());
    TEST_ASSERT_EQUAL_UINT32(0, d.discardBytes.bytes());
    TEST_ASSERT_EQUAL_UINT32(0, d.discardWave.portions());
}

// Without live outputs (kTapeOutput = false) the tape moves with MOTOR into discard, as before, and no
// request is ever answered (nobody lends DATA IN).
void test_without_live_outputs_the_tape_moves_into_discard()
{
    Deck d(recordAndRun(), false);
    for (int i = 0; i < 20000 && !d.tape.player.atEnd(); ++i) {
        (void)d.step();
    }
    TEST_ASSERT_TRUE(d.tape.player.atEnd());
    TEST_ASSERT_EQUAL_UINT32(4, d.discardBytes.bytes());
    TEST_ASSERT_TRUE(d.discardWave.portions() > 0);
    TEST_ASSERT_EQUAL_UINT32(0, d.line.pumps);
}

// Step 6: on a release the player goes to discard first (the tape stops: MOTOR gated) and Disk is confirmed
// only once the line is back on UART2. While giveBack() is Busy, no Disk; the disk resumes after.
void test_a_release_selects_discard_first_and_confirms_disk_once_the_line_is_back()
{
    Deck d(fuji() + chunk("data", 0, Bytes(200, 0x55)));
    for (int i = 0; i < 400 && d.arbiter.owner() != DataInOwner::Tape; ++i) {
        (void)d.step();
    }
    TEST_ASSERT_TRUE(d.arbiter.owner() == DataInOwner::Tape);
    for (int i = 0; i < 50; ++i) {
        (void)d.step();  // some of the record goes out
    }
    TEST_ASSERT_TRUE(d.outputs.live());
    d.line.answer = LiveLine::GiveBack::Busy;
    d.motor.motor = false;  // MOTOR off...
    DataInInputs stop = inputs(false);
    stop.commandLow = true;  // ...and the Atari talks to a disk: rule 3, back to the disk
    TEST_ASSERT_TRUE(d.arbiter.step(stop, d.now) == DataInChange::ReleaseRequested);
    for (int i = 0; i < 20; ++i) {
        d.tape.produce();
        d.now += kTickUs;
        d.tick.tick(d.now);
        TEST_ASSERT_TRUE(d.arbiter.step(stop, d.now) == DataInChange::None);
    }
    TEST_ASSERT_FALSE(d.outputs.live());  // discard from the first answered tick on
    TEST_ASSERT_TRUE(d.line.giveBacks >= 20);
    TEST_ASSERT_EQUAL(0, d.disk.resumes);
    const std::size_t sent = d.liveBytes.sent.size();
    d.line.answer = LiveLine::GiveBack::Uart;
    d.now += kTickUs;
    d.tick.tick(d.now);
    TEST_ASSERT_TRUE(d.arbiter.step(stop, d.now) == DataInChange::ToDisk);
    TEST_ASSERT_EQUAL(1, d.disk.resumes);
    TEST_ASSERT_EQUAL_UINT32(sent, d.liveBytes.sent.size());  // nothing more live after the release
}

// A line that cannot go back to UART2 (Lost): the cassette confirms Released, never Disk.
void test_a_lost_line_is_confirmed_released()
{
    Deck d(fuji() + chunk("data", 0, Bytes(200, 0x55)));
    for (int i = 0; i < 400 && d.arbiter.owner() != DataInOwner::Tape; ++i) {
        (void)d.step();
    }
    d.line.answer = LiveLine::GiveBack::Lost;
    const uint32_t seq = d.handover.request(rb::host::HandoverRequest::Release);
    d.now += kTickUs;
    d.tick.tick(d.now);
    rb::host::HandoverAck ack = rb::host::HandoverAck::Refused;
    TEST_ASSERT_TRUE(d.handover.ackFor(seq, ack));
    TEST_ASSERT_TRUE(ack == rb::host::HandoverAck::Released);
    TEST_ASSERT_FALSE(d.outputs.live());
}

// ---- Requirement 2: ended() -> TapeProgress -> arbiter, with the real EspDataIn ----

namespace {

rb::board::CassetteOutputAssignment granted()
{
    rb::board::BoardProfile p = rb::board::fujinet_atari_v1x_profile();
    p.sioGateApproved = true;
    p.motorGateApproved = true;
    return rb::board::validate_for_cassette_output(p).value();
}

alignas(rb::esp::EspDataIn) unsigned char g_dataInStorage[sizeof(rb::esp::EspDataIn)];

}  // namespace

// The tape at its end with its last byte still in UART2 and MOTOR off: no release. Once UART2 has sent it,
// ReleaseRequested, then Disk.
void test_the_published_end_waits_for_uart2_and_then_gives_the_line_back()
{
    fake_idf::reset();
    auto& s = fake_idf::state();
    s.txModel = true;
    s.uartBaud = 19200;
    (void)uart_driver_install(UART_NUM_2, 1024, 2048, 0, nullptr, 0);
    (void)uart_set_pin(UART_NUM_2, 21, 33, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    auto* dataIn = new (g_dataInStorage) rb::esp::EspDataIn();
    TEST_ASSERT_TRUE(dataIn->attachShared(granted(), 2048));

    rb::cassette::DiscardByteOut discardBytes;
    rb::cassette::DiscardWaveOut discardWave;
    PlayerOutputs outputs{discardBytes, discardWave};
    outputs.setLive(*dataIn, *dataIn);
    FakeMotor motor;
    Tape tape(fuji() + chunk("data", 0, {0x11, 0x22, 0x33}), discardWave);
    CassetteTick tick{tape.player, outputs, motor};
    DataInHandover handover;
    rb::menusio::HandoverLender lender{handover};
    rb::host::TapeProgress progress;
    rb::menusio::ProgressSink sink{progress};
    tick.lend(lender, *dataIn);
    tick.report(sink);
    FakeDisk disk;
    HandoverTapeLine tapeLine{handover};
    DataInArbiter arbiter{disk, tapeLine};

    auto step = [&]() {
        tape.produce();
        tick.tick(static_cast<uint64_t>(s.nowUs));
        fake_idf::tx_advance(static_cast<int64_t>(kTickUs));
        DataInInputs in = inputs(motor.motor);
        in.tapeEnded = rb::menusio::tapeEnded(progress);
        return arbiter.step(in, static_cast<uint64_t>(s.nowUs));
    };
    for (int i = 0; i < 400 && arbiter.owner() != DataInOwner::Tape; ++i) {
        (void)step();
    }
    TEST_ASSERT_TRUE(arbiter.owner() == DataInOwner::Tape);
    for (int i = 0; i < 20000 && !tape.player.atEnd(); ++i) {
        (void)step();
    }
    TEST_ASSERT_TRUE(tape.player.atEnd());
    motor.motor = false;
    TEST_ASSERT_FALSE(dataIn->drained());  // the last byte is still on its way out
    int held = 0;
    DataInChange change = DataInChange::None;
    for (int i = 0; i < 200 && change == DataInChange::None; ++i) {
        const bool owed = !dataIn->drained();
        change = step();
        if (change == DataInChange::None) {
            TEST_ASSERT_TRUE(arbiter.owner() == DataInOwner::Tape);
            held += owed ? 1 : 0;
        }
    }
    TEST_ASSERT_TRUE(held > 0);
    TEST_ASSERT_TRUE(change == DataInChange::ReleaseRequested);
    TEST_ASSERT_TRUE(dataIn->drained());
    for (int i = 0; i < 50 && change != DataInChange::ToDisk; ++i) {
        change = step();
    }
    TEST_ASSERT_TRUE(change == DataInChange::ToDisk);
    TEST_ASSERT_EQUAL(1, disk.resumes);
    const Bytes all{0x11, 0x22, 0x33};
    TEST_ASSERT_TRUE(s.written == all);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_pending_take_loses_nothing_and_the_whole_tape_goes_live);
    RUN_TEST(test_without_live_outputs_the_tape_moves_into_discard);
    RUN_TEST(test_a_release_selects_discard_first_and_confirms_disk_once_the_line_is_back);
    RUN_TEST(test_a_lost_line_is_confirmed_released);
    RUN_TEST(test_the_published_end_waits_for_uart2_and_then_gives_the_line_back);
    return UNITY_END();
}
