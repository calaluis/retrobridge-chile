// CassettePlayer and CassetteDeck (spec cassette-cas, C002): the tape moves only
// with MOTOR on, waits each gap, sends the rate before a record and paces the
// bytes at 10 bits per byte; MOTOR off freezes it where it is (AC6, AC7, AC12).

#include <cstdint>
#include <vector>

#include "cas_image.hpp"
#include "cassette_deck.hpp"
#include "cassette_player.hpp"
#include "unity.h"

using rb::core::Span;
using rb::host::CasResult;
using rb::host::CassetteDeck;
using rb::host::CassettePlayer;
using rb::host::TapeLink;

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

class FakeLink final : public TapeLink {
public:
    bool motor = true;
    bool failSend = false;
    std::vector<uint32_t> bauds;
    Bytes sent;
    unsigned sends = 0;
    std::size_t largestSend = 0;

    bool motorOn() const noexcept override { return motor; }
    rb::core::Result<void> setBaud(uint32_t baud) override
    {
        bauds.push_back(baud);
        return {};
    }
    rb::core::Result<void> sendTapeBytes(Span<const uint8_t> bytes) override
    {
        if (failSend) {
            return rb::core::Failure{rb::core::ErrorCode::TransportError};
        }
        ++sends;
        largestSend = bytes.size() > largestSend ? bytes.size() : largestSend;
        sent.insert(sent.end(), bytes.begin(), bytes.end());
        return {};
    }
};

constexpr uint64_t kByte600 = 16667;  // 10 bits at 600 baud, rounded up
constexpr uint64_t kByte800 = 12500;  // 10 bits at 800 baud

struct Rig {
    CassetteDeck deck;
    FakeLink link;
    uint64_t now = 0;

    explicit Rig(const Bytes& file)
    {
        TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Ok),
                          static_cast<int>(deck.mount("TEST.CAS", Span<const uint8_t>(file.data(), file.size()))));
        deck.tick(now, link);
    }
    void advance(uint64_t us)
    {
        now += us;
        deck.tick(now, link);
    }
};

}  // namespace

void setUp() {}
void tearDown() {}

// AC6: the gap passes first, then the rate is sent, then the bytes at 10 bits each.
void test_gap_then_rate_then_paced_bytes()
{
    Rig rig(fuji() + chunk("data", 100, {0x55, 0x55, 0xFC}));
    rig.advance(99999);
    TEST_ASSERT_EQUAL_UINT32(0, rig.link.sent.size());
    rig.advance(1);  // gap over
    TEST_ASSERT_EQUAL_UINT32(1, rig.link.bauds.size());
    TEST_ASSERT_EQUAL_UINT32(600, rig.link.bauds[0]);
    TEST_ASSERT_EQUAL_UINT32(0, rig.link.sent.size());
    rig.advance(kByte600);
    TEST_ASSERT_EQUAL_UINT32(1, rig.link.sent.size());
    rig.advance(2 * kByte600);
    TEST_ASSERT_EQUAL_UINT32(3, rig.link.sent.size());
    TEST_ASSERT_EQUAL_UINT8(0xFC, rig.link.sent[2]);
    TEST_ASSERT_TRUE(rig.deck.atEnd());
}

// AC6: a later record at 800 baud sends the new rate once, then paces at 800.
void test_rate_change_between_records()
{
    Rig rig(fuji() + chunk("data", 0, {1}) + chunk("baud", 800, {}) + chunk("data", 0, {2, 3}) +
            chunk("data", 0, {4}));
    rig.advance(kByte600);
    TEST_ASSERT_EQUAL_UINT32(1, rig.link.sent.size());
    rig.advance(kByte800);
    TEST_ASSERT_EQUAL_UINT32(2, rig.link.sent.size());
    rig.advance(2 * kByte800);
    TEST_ASSERT_EQUAL_UINT32(4, rig.link.sent.size());
    TEST_ASSERT_EQUAL_UINT32(2, rig.link.bauds.size());  // 600, then 800 once for two records
    TEST_ASSERT_EQUAL_UINT32(800, rig.link.bauds[1]);
}

// AC7: MOTOR off in the middle of a record freezes the tape; on again, it goes on from there.
void test_motor_off_freezes_and_resumes()
{
    Rig rig(fuji() + chunk("data", 0, {1, 2, 3, 4}));
    rig.advance(2 * kByte600);
    TEST_ASSERT_EQUAL_UINT32(2, rig.link.sent.size());
    rig.link.motor = false;
    rig.advance(10000000);  // ten seconds stopped
    TEST_ASSERT_EQUAL_UINT32(2, rig.link.sent.size());
    rig.link.motor = true;
    rig.advance(1);  // the stopped time does not count
    TEST_ASSERT_EQUAL_UINT32(2, rig.link.sent.size());
    rig.advance(kByte600);
    TEST_ASSERT_EQUAL_UINT32(3, rig.link.sent.size());
    TEST_ASSERT_EQUAL_UINT8(3, rig.link.sent[2]);
}

// MOTOR off during a gap: the gap waits too.
void test_motor_off_freezes_the_gap()
{
    Rig rig(fuji() + chunk("data", 50, {7}));
    rig.advance(30000);
    rig.link.motor = false;
    rig.advance(60000);
    rig.link.motor = true;
    rig.advance(500);  // the tick that sees MOTOR on again counts nothing
    rig.advance(19999);
    TEST_ASSERT_EQUAL_UINT32(0, rig.link.bauds.size());
    rig.advance(1 + kByte600);
    TEST_ASSERT_EQUAL_UINT32(1, rig.link.sent.size());
}

// A silence block (fsk in a level 1 tape) takes its time and sends nothing.
void test_silence_takes_time_and_sends_nothing()
{
    Rig rig(fuji() + chunk("data", 0, {1}) + chunk("fsk ", 0, {0x64, 0x00}) + chunk("data", 0, {2}));
    rig.advance(kByte600);
    TEST_ASSERT_EQUAL_UINT32(1, rig.link.sent.size());
    rig.advance(9999 + kByte600);  // 100 tenths of a ms = 10000 us of silence, minus 1
    TEST_ASSERT_EQUAL_UINT32(1, rig.link.sent.size());
    rig.advance(1);
    TEST_ASSERT_EQUAL_UINT32(2, rig.link.sent.size());
}

// The end of the tape sends nothing more, however long MOTOR stays on.
void test_end_of_tape_sends_nothing_more()
{
    Rig rig(fuji() + chunk("data", 0, {1}));
    rig.advance(kByte600);
    rig.advance(60000000);
    TEST_ASSERT_EQUAL_UINT32(1, rig.link.sent.size());
    TEST_ASSERT_TRUE(rig.deck.atEnd());
}

// A failed send does not move the tape: the same bytes go out on the next tick.
void test_failed_send_does_not_move_the_tape()
{
    Rig rig(fuji() + chunk("data", 0, {9, 8}));
    rig.link.failSend = true;
    rig.advance(kByte600);
    TEST_ASSERT_EQUAL_UINT32(0, rig.link.sent.size());
    rig.link.failSend = false;
    rig.advance(1);
    TEST_ASSERT_EQUAL_UINT32(1, rig.link.sent.size());
    TEST_ASSERT_EQUAL_UINT8(9, rig.link.sent[0]);
}

// A long tick never sends more than one data block of 512 bytes at once.
void test_sends_are_at_most_512_bytes()
{
    Rig rig(fuji() + chunk("data", 0, Bytes(1000, 0xAA)));
    rig.advance(1000 * kByte600);
    TEST_ASSERT_EQUAL_UINT32(1000, rig.link.sent.size());
    TEST_ASSERT_TRUE(rig.link.largestSend <= 512);
}

// Pacing does not drift: 600 bytes at 600 baud take exactly 10 seconds.
void test_pacing_does_not_drift()
{
    Rig rig(fuji() + chunk("data", 0, Bytes(600, 0x11)));
    for (int i = 0; i < 100; ++i) {
        rig.advance(99999);
    }
    TEST_ASSERT_EQUAL_UINT32(599, rig.link.sent.size());  // 9 999 900 us: one byte short
    rig.advance(100);
    TEST_ASSERT_EQUAL_UINT32(600, rig.link.sent.size());
}

// AC12 (deck): rewind goes back to the start and sends the rate again.
void test_rewind_restarts_the_tape()
{
    Rig rig(fuji() + chunk("data", 0, {5, 6}));
    rig.advance(2 * kByte600);
    TEST_ASSERT_TRUE(rig.deck.atEnd());
    rig.deck.rewind();
    TEST_ASSERT_FALSE(rig.deck.atEnd());
    TEST_ASSERT_EQUAL_STRING("TEST.CAS", rig.deck.name());
    rig.advance(kByte600);
    TEST_ASSERT_EQUAL_UINT32(3, rig.link.sent.size());
    TEST_ASSERT_EQUAL_UINT8(5, rig.link.sent[2]);
    TEST_ASSERT_EQUAL_UINT32(2, rig.link.bauds.size());
}

// AC8/AC9: a refused tape leaves C: as it was.
void test_refused_tape_keeps_the_mounted_one()
{
    Rig rig(fuji() + chunk("data", 0, {1}));
    const Bytes pulses = fuji() + chunk("fsk ", 0, {0x10, 0x00});
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Pulses),
                      static_cast<int>(rig.deck.mount("B.CAS", Span<const uint8_t>(pulses.data(), pulses.size()))));
    TEST_ASSERT_EQUAL_STRING("TEST.CAS", rig.deck.name());
    const Bytes broken = chunk("data", 0, {1});
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Invalid),
                      static_cast<int>(rig.deck.mount("X.CAS", Span<const uint8_t>(broken.data(), broken.size()))));
    TEST_ASSERT_EQUAL_STRING("TEST.CAS", rig.deck.name());
}

// Eject empties C:; an empty deck sends nothing.
void test_eject_empties_the_deck()
{
    Rig rig(fuji() + chunk("data", 0, {1}));
    rig.deck.eject();
    TEST_ASSERT_FALSE(rig.deck.mounted());
    TEST_ASSERT_EQUAL_STRING("", rig.deck.name());
    rig.advance(10 * kByte600);
    TEST_ASSERT_EQUAL_UINT32(0, rig.link.sent.size());
}

// Mounting a new tape replaces the old one and starts at its beginning.
void test_mount_replaces_and_starts_at_the_beginning()
{
    Rig rig(fuji() + chunk("data", 0, {1, 2}));
    rig.advance(kByte600);
    const Bytes other = fuji() + chunk("data", 0, {9});
    TEST_ASSERT_EQUAL(static_cast<int>(CasResult::Ok),
                      static_cast<int>(rig.deck.mount("B.CAS", Span<const uint8_t>(other.data(), other.size()))));
    rig.advance(kByte600);
    TEST_ASSERT_EQUAL_UINT32(2, rig.link.sent.size());
    TEST_ASSERT_EQUAL_UINT8(9, rig.link.sent[1]);
}

// A refused send does not leave a backlog: once accepted, the bytes keep their pace.
void test_refused_time_is_not_sent_as_a_burst()
{
    Rig rig(fuji() + chunk("data", 0, Bytes(100, 0x22)));
    rig.link.failSend = true;
    rig.advance(1000000);  // one second refused
    rig.link.failSend = false;
    rig.advance(1);
    TEST_ASSERT_EQUAL_UINT32(1, rig.link.sent.size());
    rig.advance(kByte600);
    TEST_ASSERT_EQUAL_UINT32(2, rig.link.sent.size());
}

// A clock that goes back counts nothing, and the time it gives back is not counted twice.
void test_clock_going_back_counts_nothing()
{
    Rig rig(fuji() + chunk("data", 100, Bytes(10, 0x33)));
    rig.advance(100000 + kByte600);  // leaves room to go back without wrapping
    TEST_ASSERT_EQUAL_UINT32(1, rig.link.sent.size());
    rig.now -= 5 * kByte600;
    rig.deck.tick(rig.now, rig.link);
    rig.now += 5 * kByte600;  // back where it was
    rig.deck.tick(rig.now, rig.link);
    TEST_ASSERT_EQUAL_UINT32(1, rig.link.sent.size());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_gap_then_rate_then_paced_bytes);
    RUN_TEST(test_rate_change_between_records);
    RUN_TEST(test_motor_off_freezes_and_resumes);
    RUN_TEST(test_motor_off_freezes_the_gap);
    RUN_TEST(test_silence_takes_time_and_sends_nothing);
    RUN_TEST(test_end_of_tape_sends_nothing_more);
    RUN_TEST(test_failed_send_does_not_move_the_tape);
    RUN_TEST(test_sends_are_at_most_512_bytes);
    RUN_TEST(test_pacing_does_not_drift);
    RUN_TEST(test_rewind_restarts_the_tape);
    RUN_TEST(test_refused_tape_keeps_the_mounted_one);
    RUN_TEST(test_eject_empties_the_deck);
    RUN_TEST(test_mount_replaces_and_starts_at_the_beginning);
    RUN_TEST(test_refused_time_is_not_sent_as_a_burst);
    RUN_TEST(test_clock_going_back_counts_nothing);
    return UNITY_END();
}
