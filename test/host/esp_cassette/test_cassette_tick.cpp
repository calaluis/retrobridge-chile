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
#include "rb/esp/esp_sio_uart.hpp"
#include "rb/menu/data_in_arbiter.hpp"
#include "rb/menu/data_in_handover.hpp"
#include "rb/menu/tape_progress.hpp"
#include "sio_disk_line.hpp"
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
alignas(rb::esp::EspSioUart) unsigned char g_sioStorage[sizeof(rb::esp::EspSioUart)];

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
    for (int i = 0; i < 20000 && s.written.size() < 3; ++i) {
        (void)step();
    }
    TEST_ASSERT_EQUAL_size_t(3, s.written.size());  // the last byte handed over
    TEST_ASSERT_FALSE(dataIn->drained());           // the last byte is still on its way out
    TEST_ASSERT_FALSE(tape.player.atEnd());         // issue #126: the record is not over yet
    motor.motor = false;
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

// Review of PR #106: the tape at its end with its last byte still in UART2, and the Atari talks to a disk
// (MOTOR off, COMMAND low: rule 3 releases at once). After select(false) the player looks at the discard
// outputs, which are always drained: TapeProgress must still say "not ended" until giveBack() has the line
// back, because the tape's byte is still going out on GPIO21.
namespace {

// The cassette task and the SIO task on the real EspDataIn in shared mode (600 baud, fake_idf's TX model),
// played to the end of the tape with its last byte still in UART2.
struct SharedChain {
    fake_idf::State& s;
    rb::esp::EspDataIn* dataIn = nullptr;
    rb::cassette::DiscardByteOut discardBytes;
    rb::cassette::DiscardWaveOut discardWave;
    PlayerOutputs outputs{discardBytes, discardWave};
    FakeMotor motor;
    Tape tape;
    CassetteTick tick{tape.player, outputs, motor};
    DataInHandover handover;
    rb::menusio::HandoverLender lender{handover};
    rb::host::TapeProgress progress;
    rb::menusio::ProgressSink sink{progress};
    FakeDisk disk;
    HandoverTapeLine tapeLine{handover};
    DataInArbiter arbiter{disk, tapeLine};
    bool commandLow = false;

    static fake_idf::State& freshIdf()
    {
        fake_idf::reset();
        auto& st = fake_idf::state();
        st.txModel = true;
        st.uartBaud = 600;
        (void)uart_driver_install(UART_NUM_2, 1024, 2048, 0, nullptr, 0);
        (void)uart_set_pin(UART_NUM_2, 21, 33, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        return st;
    }

    SharedChain() : s(freshIdf()), tape(fuji() + chunk("data", 0, {0x11, 0x22, 0x33}), discardWave)
    {
        dataIn = new (g_dataInStorage) rb::esp::EspDataIn();
        TEST_ASSERT_TRUE(dataIn->attachShared(granted(), 2048));
        outputs.setLive(*dataIn, *dataIn);
        tick.lend(lender, *dataIn);
        tick.report(sink);
    }

    DataInChange step()
    {
        tape.produce();
        tick.tick(static_cast<uint64_t>(s.nowUs));
        fake_idf::tx_advance(static_cast<int64_t>(kTickUs));
        DataInInputs in = inputs(motor.motor);
        in.commandLow = commandLow;
        in.tapeEnded = rb::menusio::tapeEnded(progress);
        return arbiter.step(in, static_cast<uint64_t>(s.nowUs));
    }

    bool ended() const { return rb::menusio::tapeEnded(progress); }

    // To the end of the tape, live, with the last byte still going out; then MOTOR off and COMMAND low (rule 3).
    void playToEndAndRelease()
    {
        for (int i = 0; i < 400 && arbiter.owner() != DataInOwner::Tape; ++i) {
            (void)step();
        }
        TEST_ASSERT_TRUE(arbiter.owner() == DataInOwner::Tape);
        for (int i = 0; i < 20000 && s.written.size() < 3; ++i) {
            (void)step();
        }
        TEST_ASSERT_EQUAL_size_t(3, s.written.size());  // the last byte handed over
        TEST_ASSERT_FALSE(dataIn->drained());           // the last byte is still on its way out
        TEST_ASSERT_FALSE(tape.player.atEnd());         // issue #126: the record is not over yet
        motor.motor = false;
        commandLow = true;
        TEST_ASSERT_TRUE(step() == DataInChange::ReleaseRequested);
    }
};

}  // namespace

void test_a_release_with_bytes_left_in_uart2_does_not_publish_the_end()
{
    SharedChain c;
    c.playToEndAndRelease();
    DataInChange change = DataInChange::None;
    int owed = 0;
    for (int i = 0; i < 200 && change != DataInChange::ToDisk; ++i) {
        if (!c.dataIn->drained()) {
            ++owed;
            TEST_ASSERT_FALSE_MESSAGE(c.ended(), "end published with a tape byte in UART2");
        }
        change = c.step();
    }
    TEST_ASSERT_TRUE(owed >= 2);  // the release was answered while the byte was still going out
    TEST_ASSERT_TRUE(change == DataInChange::ToDisk);
    TEST_ASSERT_FALSE(c.outputs.live());
    c.tick.tick(static_cast<uint64_t>(c.s.nowUs));  // the line came back after this tick's publish
    TEST_ASSERT_TRUE(c.ended());                     // the line is back: now the end is real
    const Bytes all{0x11, 0x22, 0x33};
    TEST_ASSERT_TRUE(c.s.written == all);
}

// Second review of PR #106: giveBack() answers Lost (UART2 did not take GPIO21 back) before it looks at UART2,
// with the tape's last byte still in it. Lost frees the line for safety; it says nothing about the byte. The
// end is never published after it, not even once UART2 is empty: nobody knows what reached the Atari.
void test_a_lost_line_with_bytes_left_in_uart2_never_publishes_the_end()
{
    SharedChain c;
    c.playToEndAndRelease();
    c.s.failUartSetPin = true;  // the route back to UART2 fails: Lost
    DataInChange change = DataInChange::None;
    bool released = false;
    int owedAfterLost = 0;
    for (int i = 0; i < 300; ++i) {
        change = c.step();
        released = released || change == DataInChange::Released;
        TEST_ASSERT_TRUE_MESSAGE(change != DataInChange::ToDisk, "a lost line went back to the disk");
        TEST_ASSERT_FALSE_MESSAGE(c.ended(), "end published after a lost line");
        owedAfterLost += released && !c.dataIn->drained() ? 1 : 0;
    }
    TEST_ASSERT_TRUE(released);
    TEST_ASSERT_TRUE(owedAfterLost >= 1);  // Lost was confirmed with the byte still in UART2
    TEST_ASSERT_TRUE(c.dataIn->drained());  // ...and UART2 emptied later: still no end
    TEST_ASSERT_FALSE(c.outputs.live());
}

// After a Lost release, a new confirmed take starts afresh: the next end of tape is published again.
void test_a_new_take_after_a_lost_line_publishes_the_end_again()
{
    Deck d(fuji() + chunk("data", 0, Bytes(20, 0x55)));
    for (int i = 0; i < 400 && d.arbiter.owner() != DataInOwner::Tape; ++i) {
        (void)d.step();
    }
    TEST_ASSERT_TRUE(d.outputs.live());
    d.line.answer = LiveLine::GiveBack::Lost;
    uint32_t seq = d.handover.request(rb::host::HandoverRequest::Release);
    d.now += kTickUs;
    d.tick.tick(d.now);
    rb::host::HandoverAck ack = rb::host::HandoverAck::Refused;
    TEST_ASSERT_TRUE(d.handover.ackFor(seq, ack));
    TEST_ASSERT_TRUE(ack == rb::host::HandoverAck::Released);
    seq = d.handover.request(rb::host::HandoverRequest::Take);  // the line works again (recover())
    d.now += kTickUs;
    d.tick.tick(d.now);
    TEST_ASSERT_TRUE(d.handover.ackFor(seq, ack));
    TEST_ASSERT_TRUE(ack == rb::host::HandoverAck::Tape);
    for (int i = 0; i < 20000 && !d.tape.player.atEnd(); ++i) {
        d.tape.produce();
        d.now += kTickUs;
        d.tick.tick(d.now);
    }
    TEST_ASSERT_TRUE(d.tape.player.atEnd());
    d.now += kTickUs;
    d.tick.tick(d.now);
    TEST_ASSERT_TRUE(rb::menusio::tapeEnded(d.progress));
}

// Third review of PR #106: after Lost, the arbiter (Released) asks the release again every 100 ms. Once the
// line comes back to UART2 (Uart, TX empty), Disk is confirmed and the end is published again, with no new take.
void test_a_released_line_comes_back_on_a_new_release_and_the_end_is_published_again()
{
    Deck d(fuji() + chunk("data", 0, Bytes(20, 0x55)));
    for (int i = 0; i < 20000 && !d.tape.player.atEnd(); ++i) {
        (void)d.step();
    }
    TEST_ASSERT_TRUE(d.tape.player.atEnd());
    TEST_ASSERT_TRUE(d.arbiter.owner() == DataInOwner::Tape);
    d.line.answer = LiveLine::GiveBack::Lost;
    d.motor.motor = false;  // end of tape and MOTOR off: rule 3
    DataInChange change = DataInChange::None;
    for (int i = 0; i < 50 && change != DataInChange::Released; ++i) {
        change = d.step();
    }
    TEST_ASSERT_TRUE(change == DataInChange::Released);
    for (int i = 0; i < 10; ++i) {
        (void)d.step();
        TEST_ASSERT_FALSE(rb::menusio::tapeEnded(d.progress));  // Lost: no end
    }
    d.line.answer = LiveLine::GiveBack::Uart;  // UART2 has GPIO21 again, TX empty
    for (int i = 0; i < 200 && change != DataInChange::ToDisk; ++i) {
        change = d.step();
    }
    TEST_ASSERT_TRUE(change == DataInChange::ToDisk);
    TEST_ASSERT_EQUAL(1, d.disk.resumes);
    (void)d.step();
    TEST_ASSERT_TRUE(rb::menusio::tapeEnded(d.progress));
}

// ---- T9a.7b: the whole handover on one UART2, the real disk line and the real cassette line ----

namespace {

// The menu's UART2 (EspSioUart, installed once with the TX ring of decision 3) lent to the cassette's
// EspDataIn in shared mode through the arbiter, DataInHandover and CassetteTick, on fake_idf's TX model.
struct SioAndTape {
    fake_idf::State& s;
    rb::esp::EspSioUart* sio = nullptr;
    rb::esp::EspDataIn* dataIn = nullptr;
    rb::cassette::DiscardByteOut discardBytes;
    rb::cassette::DiscardWaveOut discardWave;
    PlayerOutputs outputs{discardBytes, discardWave};
    FakeMotor motor;
    Tape tape;
    CassetteTick tick{tape.player, outputs, motor};
    DataInHandover handover;
    rb::menusio::HandoverLender lender{handover};
    rb::host::TapeProgress progress;
    rb::menusio::ProgressSink sink{progress};
    rb::menusio::UartDiskLine disk;
    HandoverTapeLine tapeLine{handover};
    DataInArbiter arbiter{disk, tapeLine};

    static fake_idf::State& freshIdf()
    {
        fake_idf::reset();
        auto& st = fake_idf::state();
        st.txModel = true;
        st.commandPin = 39;
        return st;
    }

    SioAndTape() : s(freshIdf()), tape(fuji() + chunk("data", 0, {0x11, 0x22, 0x33}), discardWave)
    {
        sio = new (g_sioStorage) rb::esp::EspSioUart{rb::esp::EspSioUart::Pins{39, 33, 21},
                                                     rb::esp::EspSioUart::kSharedTxRing};
        TEST_ASSERT_TRUE(sio->begin());  // the disk installs UART2 once, at boot
        disk.attach(*sio);
        dataIn = new (g_dataInStorage) rb::esp::EspDataIn();
        TEST_ASSERT_TRUE(dataIn->attachShared(granted(), rb::esp::EspSioUart::kSharedTxRing));
        outputs.setLive(*dataIn, *dataIn);
        tick.lend(lender, *dataIn);
        tick.report(sink);
    }

    DataInChange step()
    {
        tape.produce();
        tick.tick(static_cast<uint64_t>(s.nowUs));
        fake_idf::tx_advance(static_cast<int64_t>(kTickUs));
        DataInInputs in = inputs(motor.motor);
        in.tapeEnded = rb::menusio::tapeEnded(progress);
        const DataInChange change = arbiter.step(in, static_cast<uint64_t>(s.nowUs));
        TEST_ASSERT_FALSE_MESSAGE(!sio->parked() && outputs.live(), "the disk and the tape both own DATA IN");
        return change;
    }

    bool diskAnswers()
    {
        const uint8_t answer[1] = {0x41};
        return sio->write(Span<const uint8_t>{answer, 1}).ok();
    }
};

}  // namespace

// T9a.7b: D1 answers at 19200; MOTOR on parks the disk (driver kept) and the tape plays at 600 baud on the
// same UART2; at the end, with MOTOR off, the disk gets the line back at 19200, verified, its input flushed,
// and answers again. UART2 is installed once and never deleted.
void test_the_disk_lends_uart2_to_the_tape_and_gets_it_back()
{
    SioAndTape c;
    TEST_ASSERT_TRUE(c.diskAnswers());
    c.s.written.clear();
    DataInChange change = DataInChange::None;
    bool toTape = false;
    for (int i = 0; i < 400 && !toTape; ++i) {
        change = c.step();
        toTape = change == DataInChange::ToTape;
    }
    TEST_ASSERT_TRUE(toTape);
    TEST_ASSERT_TRUE(c.sio->parked());
    TEST_ASSERT_FALSE(c.diskAnswers());  // parked: D1 silent while the tape owns DATA IN
    for (int i = 0; i < 20000 && !c.tape.player.atEnd(); ++i) {
        (void)c.step();
    }
    TEST_ASSERT_TRUE(c.tape.player.atEnd());
    TEST_ASSERT_EQUAL_INT(600, c.s.uartBaud);
    c.motor.motor = false;
    c.s.rx = {0xAA, 0x55};  // what the Atari sent at 19200, read at 600: garbage for the disk
    for (int i = 0; i < 400 && change != DataInChange::ToDisk; ++i) {
        change = c.step();
    }
    TEST_ASSERT_TRUE(change == DataInChange::ToDisk);
    TEST_ASSERT_FALSE(c.sio->parked());
    TEST_ASSERT_EQUAL_INT(19200, c.s.uartBaud);
    TEST_ASSERT_EQUAL_UINT(0, c.s.rx.size());
    const Bytes tape{0x11, 0x22, 0x33};
    TEST_ASSERT_TRUE(c.s.written == tape);
    TEST_ASSERT_TRUE(c.diskAnswers());
    TEST_ASSERT_EQUAL_UINT(1, c.s.uartInstalls);
    TEST_ASSERT_EQUAL_UINT(0, c.s.uartDeletes);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_pending_take_loses_nothing_and_the_whole_tape_goes_live);
    RUN_TEST(test_without_live_outputs_the_tape_moves_into_discard);
    RUN_TEST(test_a_release_selects_discard_first_and_confirms_disk_once_the_line_is_back);
    RUN_TEST(test_a_lost_line_is_confirmed_released);
    RUN_TEST(test_the_published_end_waits_for_uart2_and_then_gives_the_line_back);
    RUN_TEST(test_a_release_with_bytes_left_in_uart2_does_not_publish_the_end);
    RUN_TEST(test_a_lost_line_with_bytes_left_in_uart2_never_publishes_the_end);
    RUN_TEST(test_a_new_take_after_a_lost_line_publishes_the_end_again);
    RUN_TEST(test_a_released_line_comes_back_on_a_new_release_and_the_end_is_published_again);
    RUN_TEST(test_the_disk_lends_uart2_to_the_tape_and_gets_it_back);
    return UNITY_END();
}
