// CasTapeDeck (spec menu-cinta, plan §3, T5): the menu's TapeDeck over the `cas` commands. The
// channel is a fake: it records what is sent and hands back the replies the test queues.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "rb/menu/cas_tape_deck.hpp"
#include "unity.h"

using rb::host::CasMountReply;
using rb::host::CasTapeDeck;
using rb::host::TapeState;

namespace {

class FakeChannel final : public rb::host::CasChannel {
public:
    bool available() const noexcept override { return up; }
    bool send(const char* args) noexcept override
    {
        if (inFlight) {
            return false;
        }
        sent.emplace_back(args);
        inFlight = true;
        return true;
    }
    bool receive(char* reply, std::size_t size) noexcept override
    {
        if (!inFlight || replies.empty()) {
            return false;
        }
        std::snprintf(reply, size, "%s", replies.front().c_str());
        replies.erase(replies.begin());
        inFlight = false;
        return true;
    }

    bool up = true;
    bool inFlight = false;
    std::vector<std::string> sent;
    std::vector<std::string> replies;  // handed out one per receive(), in order
};

void assertSent(const FakeChannel& channel, std::initializer_list<const char*> expected)
{
    TEST_ASSERT_EQUAL_size_t_MESSAGE(expected.size(), channel.sent.size(), "commands sent");
    std::size_t i = 0;
    for (const char* line : expected) {
        TEST_ASSERT_EQUAL_STRING(line, channel.sent[i++].c_str());
    }
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_the_mount_replies_are_classified()
{
    TEST_ASSERT_TRUE(rb::host::classifyMountReply("mounted GAME.CAS: 12 data, 3 fsk") == CasMountReply::Mounted);
    TEST_ASSERT_TRUE(rb::host::classifyMountReply("busy: the last tape is still closing") == CasMountReply::Busy);
    TEST_ASSERT_TRUE(rb::host::classifyMountReply("GAME.CAS: too many blocks (max 1024)") == CasMountReply::TooBig);
    TEST_ASSERT_TRUE(rb::host::classifyMountReply("cannot open GAME.CAS") == CasMountReply::Invalid);
    TEST_ASSERT_TRUE(rb::host::classifyMountReply("GAME.CAS: not a valid .cas (FUJI header, cut block or baud 0)")
                     == CasMountReply::Invalid);
    TEST_ASSERT_TRUE(rb::host::classifyMountReply("") == CasMountReply::NoReply);  // no reply in time
    TEST_ASSERT_TRUE(rb::host::classifyMountReply(nullptr) == CasMountReply::NoReply);
}

// mount -> Pending while the service reads, Ready once it says "mounted", then Idle.
void test_a_good_tape_is_pending_then_ready_once()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    TEST_ASSERT_TRUE(deck.mount("GAME.CAS"));
    assertSent(channel, {"mount GAME.CAS"});
    TEST_ASSERT_TRUE(deck.poll() == TapeState::Pending);
    channel.replies.push_back("mounted GAME.CAS: 12 data, 3 fsk");
    TEST_ASSERT_TRUE(deck.poll() == TapeState::Ready);
    TEST_ASSERT_TRUE(deck.poll() == TapeState::Idle);
    TEST_ASSERT_EQUAL_STRING("GAME.CAS", deck.loaded());
}

// AC6: a tape the service refuses is Invalid; the tape that was loaded stays.
void test_a_refused_tape_is_invalid_and_the_old_tape_stays()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount("OLD.CAS");
    channel.replies.push_back("mounted OLD.CAS: 1 data, 0 fsk");
    deck.poll();
    deck.mount("BAD.CAS");
    channel.replies.push_back("BAD.CAS: not a valid .cas (FUJI header, cut block or baud 0)");
    TEST_ASSERT_TRUE(deck.poll() == TapeState::Invalid);
    TEST_ASSERT_EQUAL_STRING("OLD.CAS", deck.loaded());
}

// AC6, second case: no PSRAM, the service never started: NoMemory, nothing sent.
void test_a_service_that_did_not_start_answers_no_memory()
{
    FakeChannel channel;
    channel.up = false;
    CasTapeDeck deck(channel, true);
    TEST_ASSERT_TRUE(deck.mount("GAME.CAS"));
    TEST_ASSERT_TRUE(deck.poll() == TapeState::NoMemory);
    TEST_ASSERT_TRUE(deck.poll() == TapeState::Idle);
    assertSent(channel, {});
}

// A tape with more blocks than the segment tables hold does not fit: NoMemory.
void test_too_many_blocks_is_no_memory()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount("HUGE.CAS");
    channel.replies.push_back("HUGE.CAS: too many blocks (max 1024)");
    TEST_ASSERT_TRUE(deck.poll() == TapeState::NoMemory);
}

// A load in flight: a second mount is refused (Busy for the menu), nothing sent.
void test_a_second_mount_while_one_is_in_flight_is_refused()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount("A.CAS");
    TEST_ASSERT_FALSE(deck.mount("B.CAS"));
    assertSent(channel, {"mount A.CAS"});
}

// "busy": the last tape is still closing; the same mount is sent again, and works.
void test_a_busy_reply_sends_the_mount_again()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount("A.CAS");
    channel.replies.push_back("busy: the last tape is still closing");
    TEST_ASSERT_TRUE(deck.poll() == TapeState::Pending);
    channel.replies.push_back("mounted A.CAS: 1 data, 0 fsk");
    TEST_ASSERT_TRUE(deck.poll() == TapeState::Ready);
    assertSent(channel, {"mount A.CAS", "mount A.CAS"});
}

// A service that stays busy: the load gives up after kBusyRetries, as Invalid; never forever.
void test_a_service_that_stays_busy_gives_up()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount("A.CAS");
    TapeState state = TapeState::Pending;
    int polls = 0;
    while (state == TapeState::Pending && polls < 100) {
        channel.replies.push_back("busy: the last tape is still closing");
        state = deck.poll();
        ++polls;
    }
    TEST_ASSERT_TRUE(state == TapeState::Invalid);
    TEST_ASSERT_EQUAL_size_t(1 + CasTapeDeck::kBusyRetries, channel.sent.size());
}

// AC6: cancel while reading, and the cassette took the new tape anyway: the old one goes back.
void test_cancel_puts_the_old_tape_back()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount("OLD.CAS");
    channel.replies.push_back("mounted OLD.CAS: 1 data, 0 fsk");
    deck.poll();
    deck.mount("NEW.CAS");
    deck.cancel();
    TEST_ASSERT_TRUE(deck.poll() == TapeState::Idle);
    channel.replies.push_back("mounted NEW.CAS: 1 data, 0 fsk");
    deck.pump();
    channel.replies.push_back("mounted OLD.CAS: 1 data, 0 fsk");
    deck.pump();
    TEST_ASSERT_TRUE(deck.poll() == TapeState::Idle);
    TEST_ASSERT_EQUAL_STRING("OLD.CAS", deck.loaded());
    assertSent(channel, {"mount OLD.CAS", "mount NEW.CAS", "mount OLD.CAS"});
}

// Cancel, and the old tape cannot come back (gone from the microSD): the new one is ejected.
void test_cancel_ejects_when_the_old_tape_cannot_come_back()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount("OLD.CAS");
    channel.replies.push_back("mounted OLD.CAS: 1 data, 0 fsk");
    deck.poll();
    deck.mount("NEW.CAS");
    deck.cancel();
    channel.replies.push_back("mounted NEW.CAS: 1 data, 0 fsk");
    deck.pump();
    channel.replies.push_back("cannot open OLD.CAS");
    deck.pump();
    TEST_ASSERT_EQUAL_STRING("", deck.loaded());
    assertSent(channel, {"mount OLD.CAS", "mount NEW.CAS", "mount OLD.CAS", "eject"});
}

// Cancel with no tape before: the new one is ejected again.
void test_cancel_with_no_old_tape_ejects_the_new_one()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount("NEW.CAS");
    deck.cancel();
    channel.replies.push_back("mounted NEW.CAS: 1 data, 0 fsk");
    deck.pump();
    channel.replies.push_back("ejected NEW.CAS");
    deck.pump();
    TEST_ASSERT_EQUAL_STRING("", deck.loaded());
    assertSent(channel, {"mount NEW.CAS", "eject"});
}

// Cancel, and the cassette refused the new tape: nothing to undo.
void test_cancel_of_a_refused_tape_sends_nothing_more()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount("BAD.CAS");
    deck.cancel();
    channel.replies.push_back("cannot open BAD.CAS");
    deck.pump();
    TEST_ASSERT_TRUE(deck.poll() == TapeState::Idle);
    assertSent(channel, {"mount BAD.CAS"});
}

// eject sends "eject"; with a command in flight it waits for the reply, then goes.
void test_eject_waits_for_the_command_in_flight()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount("A.CAS");
    channel.replies.push_back("mounted A.CAS: 1 data, 0 fsk");
    deck.poll();
    deck.eject();
    assertSent(channel, {"mount A.CAS", "eject"});
    TEST_ASSERT_EQUAL_STRING("", deck.loaded());
    // A second eject while the first is in flight waits, then goes once.
    deck.eject();
    assertSent(channel, {"mount A.CAS", "eject"});
    channel.replies.push_back("ejected A.CAS");
    deck.pump();
    assertSent(channel, {"mount A.CAS", "eject", "eject"});
    channel.replies.push_back("no tape");
    deck.pump();
    deck.pump();
    assertSent(channel, {"mount A.CAS", "eject", "eject"});
}

// A mount right after an eject is refused until the eject is answered.
void test_a_mount_waits_for_an_eject_in_flight()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.eject();
    TEST_ASSERT_FALSE(deck.mount("A.CAS"));
    channel.replies.push_back("no tape");
    deck.pump();
    TEST_ASSERT_TRUE(deck.mount("A.CAS"));
}

// AC8: the gates come from the package, as given.
void test_the_gates_are_the_ones_given()
{
    FakeChannel channel;
    TEST_ASSERT_TRUE(CasTapeDeck(channel, true).gatesOpen());
    TEST_ASSERT_FALSE(CasTapeDeck(channel, false).gatesOpen());
}

// A name too long for the command line is Invalid at once; nothing sent.
void test_a_name_too_long_is_invalid()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    char name[CasTapeDeck::kNameMax + 8];
    std::memset(name, 'A', sizeof name - 1);
    name[sizeof name - 1] = '\0';
    TEST_ASSERT_TRUE(deck.mount(name));
    TEST_ASSERT_TRUE(deck.poll() == TapeState::Invalid);
    assertSent(channel, {});
}

// Review of PR #85 (T5), 1: "busy" to an eject. The tape is still in the cassette: eject again.
void test_a_busy_eject_is_sent_again()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount("A.CAS");
    channel.replies.push_back("mounted A.CAS: 1 data, 0 fsk");
    deck.poll();
    deck.eject();
    channel.replies.push_back("busy: the last tape is still closing");
    deck.pump();
    channel.replies.push_back("ejected A.CAS");
    deck.pump();
    deck.pump();
    assertSent(channel, {"mount A.CAS", "eject", "eject"});
}

// An eject with no reply in time is sent again too (a second eject is harmless: "no tape").
void test_an_eject_with_no_reply_is_sent_again()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.eject();
    channel.replies.push_back("");
    deck.pump();
    channel.replies.push_back("no tape");
    deck.pump();
    deck.pump();
    assertSent(channel, {"eject", "eject"});
}

// Review 1: "busy" while putting the old tape back after a cancel: try the old tape again,
// never eject the new one as if the old tape were gone.
void test_a_busy_restore_is_sent_again()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount("OLD.CAS");
    channel.replies.push_back("mounted OLD.CAS: 1 data, 0 fsk");
    deck.poll();
    deck.mount("NEW.CAS");
    deck.cancel();
    channel.replies.push_back("mounted NEW.CAS: 1 data, 0 fsk");
    deck.pump();
    channel.replies.push_back("busy: the last tape is still closing");
    deck.pump();
    channel.replies.push_back("mounted OLD.CAS: 1 data, 0 fsk");
    deck.pump();
    deck.pump();
    TEST_ASSERT_EQUAL_STRING("OLD.CAS", deck.loaded());
    assertSent(channel, {"mount OLD.CAS", "mount NEW.CAS", "mount OLD.CAS", "mount OLD.CAS"});
}

// Review 2: pump() took "mounted", then the menu cancels before poll() hands out Ready. The
// cassette holds the new tape: the old one goes back, and C: keeps saying the old one.
void test_cancel_after_the_tape_was_mounted_but_not_yet_seen_puts_the_old_tape_back()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount("OLD.CAS");
    channel.replies.push_back("mounted OLD.CAS: 1 data, 0 fsk");
    deck.poll();
    deck.mount("NEW.CAS");
    channel.replies.push_back("mounted NEW.CAS: 1 data, 0 fsk");
    deck.pump();
    deck.cancel();
    TEST_ASSERT_TRUE(deck.poll() == TapeState::Idle);
    channel.replies.push_back("mounted OLD.CAS: 1 data, 0 fsk");
    deck.pump();
    TEST_ASSERT_EQUAL_STRING("OLD.CAS", deck.loaded());
    assertSent(channel, {"mount OLD.CAS", "mount NEW.CAS", "mount OLD.CAS"});
}

// Review 3: no reply in time, but the service keeps the command and may finish the mount. Ask
// the cassette what it holds ("status" runs after the mount, on the same loop): the new tape.
void test_a_mount_with_no_reply_asks_the_status_and_finds_the_new_tape()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount("SLOW.CAS");
    channel.replies.push_back("");
    TEST_ASSERT_TRUE(deck.poll() == TapeState::Pending);
    channel.replies.push_back("SLOW.CAS: segment 1/9, gap 0 ms, motor off");
    TEST_ASSERT_TRUE(deck.poll() == TapeState::Ready);
    TEST_ASSERT_EQUAL_STRING("SLOW.CAS", deck.loaded());
    assertSent(channel, {"mount SLOW.CAS", "status"});
}

// Review 3: no reply in time, and the status shows the old tape: the new one was not taken.
void test_a_mount_with_no_reply_and_the_old_tape_in_the_status_is_invalid()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount("OLD.CAS");
    channel.replies.push_back("mounted OLD.CAS: 1 data, 0 fsk");
    deck.poll();
    deck.mount("NEW.CAS");
    channel.replies.push_back("");
    deck.poll();
    channel.replies.push_back("OLD.CAS: segment 1/1, gap 0 ms, motor off");
    TEST_ASSERT_TRUE(deck.poll() == TapeState::Invalid);
    TEST_ASSERT_EQUAL_STRING("OLD.CAS", deck.loaded());
}

// Review 3: a status with no reply either is asked again, a bounded number of times.
void test_a_status_with_no_reply_is_asked_again_then_gives_up()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount("A.CAS");
    TapeState state = TapeState::Pending;
    int polls = 0;
    while (state == TapeState::Pending && polls < 100) {
        channel.replies.push_back("");
        state = deck.poll();
        ++polls;
    }
    // Review 2 of PR #85, 3: nothing confirmed either way; never the false "not valid".
    TEST_ASSERT_TRUE(state == TapeState::Unconfirmed);
    TEST_ASSERT_TRUE(polls < 100);
}

// Review 3 with a cancel: no reply in time, cancel, and the status shows the new tape: the
// old one goes back.
void test_cancel_during_a_status_check_puts_the_old_tape_back()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount("OLD.CAS");
    channel.replies.push_back("mounted OLD.CAS: 1 data, 0 fsk");
    deck.poll();
    deck.mount("NEW.CAS");
    channel.replies.push_back("");
    deck.pump();
    deck.cancel();
    channel.replies.push_back("NEW.CAS: segment 1/1, gap 0 ms, motor off");
    deck.pump();
    channel.replies.push_back("mounted OLD.CAS: 1 data, 0 fsk");
    deck.pump();
    TEST_ASSERT_EQUAL_STRING("OLD.CAS", deck.loaded());
    assertSent(channel, {"mount OLD.CAS", "mount NEW.CAS", "status", "mount OLD.CAS"});
}

// Review 2 of PR #85, 1: U then C while the remembered tape is still being read. The eject
// drops that load: when "mounted" comes, the tape goes out, no Ready is left behind, and the
// next mount is taken once the eject is answered.
void test_an_eject_during_a_load_drops_it_and_later_mounts_work()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount("A.CAS");
    deck.eject();
    channel.replies.push_back("mounted A.CAS: 1 data, 0 fsk");
    deck.pump();
    channel.replies.push_back("ejected A.CAS");
    deck.pump();
    TEST_ASSERT_TRUE(deck.poll() == TapeState::Idle);
    TEST_ASSERT_EQUAL_STRING("", deck.loaded());
    assertSent(channel, {"mount A.CAS", "eject"});
    TEST_ASSERT_TRUE(deck.mount("B.CAS"));
}

// Review 2, 1: the load was already answered (pump() took "mounted") but nobody polled it.
void test_an_eject_after_an_unread_ready_drops_it()
{
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount("A.CAS");
    channel.replies.push_back("mounted A.CAS: 1 data, 0 fsk");
    deck.pump();
    deck.eject();
    TEST_ASSERT_TRUE(deck.poll() == TapeState::Idle);
    channel.replies.push_back("ejected A.CAS");
    deck.pump();
    TEST_ASSERT_TRUE(deck.mount("B.CAS"));
}

// Review 2, 2: the cassette keeps 63 bytes of a name. Two long names with the same 63 bytes
// cannot be told apart by "status": the load is not confirmed, never Ready for the wrong tape.
void test_a_status_that_cannot_tell_two_long_names_apart_is_not_ready()
{
    const std::string head(70, 'A');
    const std::string first = head + "1.CAS";
    const std::string second = head + "2.CAS";
    FakeChannel channel;
    CasTapeDeck deck(channel, true);
    deck.mount(first.c_str());
    channel.replies.push_back("mounted " + head.substr(0, 63) + ": 1 data, 0 fsk");
    deck.poll();
    deck.mount(second.c_str());
    channel.replies.push_back("");
    deck.poll();
    channel.replies.push_back(head.substr(0, 63) + ": segment 1/1, gap 0 ms, motor off");
    TEST_ASSERT_TRUE(deck.poll() == TapeState::Unconfirmed);
}

// statusHolds: same name, another name, and a long name it cannot decide.
void test_status_holds_knows_when_it_cannot_tell()
{
    using rb::host::StatusHolds;
    TEST_ASSERT_TRUE(rb::host::statusHolds("A.CAS: segment 1/1, motor off", "A.CAS") == StatusHolds::Yes);
    TEST_ASSERT_TRUE(rb::host::statusHolds("B.CAS: end, motor off", "A.CAS") == StatusHolds::No);
    TEST_ASSERT_TRUE(rb::host::statusHolds("no tape, motor off", "A.CAS") == StatusHolds::No);
    const std::string head(70, 'A');
    const std::string reply = head.substr(0, 63) + ": segment 1/1, motor off";
    TEST_ASSERT_TRUE(rb::host::statusHolds(reply.c_str(), (head + "1.CAS").c_str()) == StatusHolds::Unknown);
    const std::string other = std::string(63, 'B') + ": segment 1/1, motor off";
    TEST_ASSERT_TRUE(rb::host::statusHolds(other.c_str(), (head + "1.CAS").c_str()) == StatusHolds::No);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_mount_replies_are_classified);
    RUN_TEST(test_a_good_tape_is_pending_then_ready_once);
    RUN_TEST(test_a_refused_tape_is_invalid_and_the_old_tape_stays);
    RUN_TEST(test_a_service_that_did_not_start_answers_no_memory);
    RUN_TEST(test_too_many_blocks_is_no_memory);
    RUN_TEST(test_a_second_mount_while_one_is_in_flight_is_refused);
    RUN_TEST(test_a_busy_reply_sends_the_mount_again);
    RUN_TEST(test_a_service_that_stays_busy_gives_up);
    RUN_TEST(test_cancel_puts_the_old_tape_back);
    RUN_TEST(test_cancel_ejects_when_the_old_tape_cannot_come_back);
    RUN_TEST(test_cancel_with_no_old_tape_ejects_the_new_one);
    RUN_TEST(test_cancel_of_a_refused_tape_sends_nothing_more);
    RUN_TEST(test_eject_waits_for_the_command_in_flight);
    RUN_TEST(test_a_mount_waits_for_an_eject_in_flight);
    RUN_TEST(test_the_gates_are_the_ones_given);
    RUN_TEST(test_a_name_too_long_is_invalid);
    RUN_TEST(test_a_busy_eject_is_sent_again);
    RUN_TEST(test_an_eject_with_no_reply_is_sent_again);
    RUN_TEST(test_a_busy_restore_is_sent_again);
    RUN_TEST(test_cancel_after_the_tape_was_mounted_but_not_yet_seen_puts_the_old_tape_back);
    RUN_TEST(test_a_mount_with_no_reply_asks_the_status_and_finds_the_new_tape);
    RUN_TEST(test_a_mount_with_no_reply_and_the_old_tape_in_the_status_is_invalid);
    RUN_TEST(test_a_status_with_no_reply_is_asked_again_then_gives_up);
    RUN_TEST(test_cancel_during_a_status_check_puts_the_old_tape_back);
    RUN_TEST(test_an_eject_during_a_load_drops_it_and_later_mounts_work);
    RUN_TEST(test_an_eject_after_an_unread_ready_drops_it);
    RUN_TEST(test_a_status_that_cannot_tell_two_long_names_apart_is_not_ready);
    RUN_TEST(test_status_holds_knows_when_it_cannot_tell);
    return UNITY_END();
}
