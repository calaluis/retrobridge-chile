// Choosing a .cas tape from the menu (spec menu-cinta). T1: the tapes of the local
// folder (RB_TEST_MENU_DIR, written by main() before the tests) are listed with the
// .atr and .xex files, and a drive digit refuses a tape: a tape only goes on C:.
// T2: C mounts a tape on C: through the TapeDeck port (a fake here), C refuses a disk,
// U then C takes the tape out, and the message line says what happened.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "menu_tnfs_rig.hpp"
#include "rb/menu/tape_deck.hpp"

namespace {

using rb::host::TapeState;

constexpr uint8_t KD = 0x3A;  // Atari keyboard code of D

// The cassette as the menu sees it: answers what the test sets, records what it is asked.
class FakeTapeDeck final : public rb::host::TapeDeck {
public:
    bool mount(const char* name) noexcept override
    {
        ++mounts;
        std::snprintf(last, sizeof last, "%s", name);  // no allocation: key() is checked for it
        if (busy) {
            return false;
        }
        ++accepted;
        pendingPolls = pendingFor;
        loading = true;
        return true;
    }
    TapeState poll() noexcept override
    {
        if (!loading) {
            return TapeState::Idle;
        }
        if (pendingPolls > 0) {
            --pendingPolls;
            return TapeState::Pending;
        }
        loading = false;
        return result;
    }
    void cancel() noexcept override
    {
        ++cancels;
        loading = false;
    }
    void eject() noexcept override { ++ejects; }
    bool gatesOpen() const noexcept override { return gates; }

    TapeState result = TapeState::Ready;
    int pendingFor = 1;
    bool busy = false;
    bool gates = true;
    int mounts = 0;    // every mount() call
    int accepted = 0;  // the ones that started a load
    int cancels = 0;
    int ejects = 0;
    char last[160] = {};

private:
    int pendingPolls = 0;
    bool loading = false;
};

constexpr const char* kLongTape = "UNA_CINTA_CON_UN_NOMBRE_MUY_LARGO_PARA_LA_LINEA.CAS";

void writeFile(const std::string& path, const Bytes& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// A "FUJI" chunk with no data: enough to be listed; the menu never reads the tape itself.
Bytes tape() { return {'F', 'U', 'J', 'I', 0, 0, 0, 0}; }

// The local folder, in menu order: A DISCO.ATR, B JUEGO.XEX, C KARATE.CAS, D nave.Cas,
// E the long tape name.
void writeFolder()
{
    const std::string dir = RB_TEST_MENU_DIR;
    std::error_code ignored;
    std::filesystem::create_directories(dir, ignored);
    writeFile(dir + "/DISCO.ATR", atr(720, 1));
    writeFile(dir + "/JUEGO.XEX", {0xFF, 0xFF, 0x00, 0x30, 0x00, 0x30, 0x60});
    writeFile(dir + "/KARATE.CAS", tape());
    writeFile(dir + "/nave.Cas", tape());
    writeFile(dir + "/" + kLongTape, tape());
    writeFile(dir + "/notas.txt", {'x'});
}

std::string tapeLine(const char* name)
{
    std::string line = std::string("C: ") + name;
    line.resize(40, ' ');
    return line;
}

// Mounts the tape of `letter` on C:.
MenuReply mountTape(Rig& rig, uint8_t letter)
{
    rig.press(K1);
    rig.press(letter);
    return rig.settle(KC);
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// AC1: the tapes are listed with their letter, among the .atr and .xex files (shown in capitals).
void test_the_tapes_are_listed_with_the_disks()
{
    Rig rig;
    rig.start();
    const MenuReply list = rig.press(K1);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutList, list.layout());
    assert_marked(list, row(0), " A  DISCO.ATR");
    assert_line(list, row(1), " B  JUEGO.XEX");
    assert_line(list, row(2), " C  KARATE.CAS");
    assert_line(list, row(3), " D  NAVE.CAS");
    std::string longRow = std::string(" E  ") + kLongTape;
    longRow.resize(34);  // the list shows 30 columns of a name
    assert_line(list, row(4), longRow.c_str());
    assert_line(list, row(5), "");
}

// AC3 (first half): a drive digit with a tape chosen says a tape goes on C:; no drive changes.
void test_a_tape_on_a_drive_says_it_goes_on_c()
{
    Rig rig;
    rig.start();
    rig.press(K1);
    assert_line(rig.press(KC), kMainMessage, " UNIDAD PARA: KARATE.CAS");
    const MenuReply refused = rig.settle(K2);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, refused.layout());
    assert_line(refused, kMainMessage, " UNA CINTA VA EN C:");
    rig.step();
    TEST_ASSERT_EQUAL_STRING("", rig.mounted(2));
    TEST_ASSERT_NULL(rig.controller->driveName(2));
    TEST_ASSERT_TRUE(rig.events.has("D2 name=KARATE.CAS refused: a tape only goes on C:"));
}

// The same with D1: the tape never becomes the boot disk.
void test_a_tape_on_d1_says_it_goes_on_c()
{
    Rig rig;
    rig.start();
    rig.press(K1);
    rig.press(KC);
    const MenuReply refused = rig.settle(K1);
    assert_line(refused, kMainMessage, " UNA CINTA VA EN C:");
    TEST_ASSERT_NULL(rig.controller->driveName(1));
}

void test_the_extension_helpers_know_a_tape()
{
    TEST_ASSERT_TRUE(rb::host::hasCasExtension("KARATE.CAS"));
    TEST_ASSERT_TRUE(rb::host::hasCasExtension("a.cAs"));
    TEST_ASSERT_FALSE(rb::host::hasCasExtension(".cas"));
    TEST_ASSERT_FALSE(rb::host::hasCasExtension("a.cass"));
    TEST_ASSERT_TRUE(rb::host::isCatalogName("KARATE.CAS"));
    TEST_ASSERT_FALSE(rb::host::hasImageExtension("KARATE.CAS"));  // TNFS lists no tapes (out of scope)
    TEST_ASSERT_TRUE(rb::host::entryKind("A.ATR") == rb::host::EntryKind::Disk);
    TEST_ASSERT_TRUE(rb::host::entryKind("A.XEX") == rb::host::EntryKind::Xex);
    TEST_ASSERT_TRUE(rb::host::entryKind("A.CAS") == rb::host::EntryKind::Tape);
}

// AC2: C with a tape chosen mounts it: the C: line shows its name, D1-D8 do not change.
void test_c_mounts_the_tape_and_the_c_line_shows_it()
{
    Rig rig;
    FakeTapeDeck deck;
    rig.start();
    rig.controller->setTapeDeck(&deck);
    const MenuReply mounted = mountTape(rig, KC);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, mounted.layout());
    assert_line(mounted, rb::host::kMainTape, tapeLine("KARATE.CAS").c_str());
    assert_line(mounted, kMainMessage, " CINTA EN C:, ENCIENDE CON START");
    TEST_ASSERT_EQUAL_STRING("KARATE.CAS", deck.last);
    TEST_ASSERT_EQUAL_STRING("KARATE.CAS", rig.controller->tapeName());
    for (unsigned n = 1; n <= 8; ++n) {
        TEST_ASSERT_NULL(rig.controller->driveName(n));
    }
    TEST_ASSERT_TRUE(rig.events.has("C: name=KARATE.CAS mounted gates=1"));
}

// AC2: while the tape is read the 6502 waits, then the line shows it; a long name is cut to the line.
void test_a_long_tape_name_is_cut_to_the_line()
{
    Rig rig;
    FakeTapeDeck deck;
    deck.pendingFor = 3;
    rig.start();
    rig.controller->setTapeDeck(&deck);
    rig.press(K1);
    rig.press(KE);
    const MenuReply reading = rig.press(KC);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionWait, reading.action());
    assert_line(reading, kMainMessage, " LEYENDO CINTA...");
    const MenuReply mounted = rig.settle(kPoll);
    std::string expected = std::string("C: ") + kLongTape;
    expected.resize(40);
    assert_line(mounted, rb::host::kMainTape, expected.c_str());
}

// AC3 (second half): C with a disk or a .xex chosen says a disk does not go on C:.
void test_c_with_a_disk_or_a_xex_says_a_disk_does_not_go_on_c()
{
    Rig rig;
    FakeTapeDeck deck;
    rig.start();
    rig.controller->setTapeDeck(&deck);
    for (uint8_t letter : {KA, KB}) {
        rig.press(K1);
        rig.press(letter);
        const MenuReply refused = rig.settle(KC);
        TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, refused.layout());
        assert_line(refused, kMainMessage, " UN DISCO NO VA EN C:");
        assert_line(refused, rb::host::kMainTape, "C: SIN CINTA");
    }
    TEST_ASSERT_EQUAL_INT(0, deck.mounts);
    TEST_ASSERT_NULL(rig.controller->tapeName());
    TEST_ASSERT_TRUE(rig.events.has("C: name=DISCO.ATR refused: a disk does not go on C:"));
}

// AC3 (second half), TNFS: a disk of a TNFS folder does not go on C: either.
void test_c_with_a_tnfs_disk_says_a_disk_does_not_go_on_c()
{
    Rig rig;
    FakeTapeDeck deck;
    rig.server.addFile("/Juegos/Dos/Tres/c.atr", atr(720, 4));
    rig.start();
    rig.controller->setTapeDeck(&deck);
    rig.walk({K2, KB, KA, KA});  // /Juegos, /Juegos/Dos, /Juegos/Dos/Tres
    rig.press(KA);                // C.ATR, waiting for a drive
    const MenuReply refused = rig.settle(KC);
    assert_line(refused, kListMessage, " UN DISCO NO VA EN C:");
    TEST_ASSERT_EQUAL_INT(0, deck.mounts);
    TEST_ASSERT_NULL(rig.controller->tapeName());
}

// AC4: U then C takes the tape out; the line says C: SIN CINTA.
void test_u_then_c_takes_the_tape_out()
{
    Rig rig;
    FakeTapeDeck deck;
    rig.start();
    rig.controller->setTapeDeck(&deck);
    mountTape(rig, KC);
    assert_line(rig.press(KU), kMainMessage, " DESMONTAR: PULSA 1-8 O C, ESC CANCELA");
    const MenuReply out = rig.settle(KC);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, out.layout());
    assert_line(out, rb::host::kMainTape, "C: SIN CINTA");
    TEST_ASSERT_EQUAL_INT(1, deck.ejects);
    TEST_ASSERT_NULL(rig.controller->tapeName());
    TEST_ASSERT_TRUE(rig.events.has("C: name=KARATE.CAS unmounted"));
}

// AC6: a tape the deck cannot read is refused; the tape already on C: stays.
void test_an_invalid_tape_or_no_memory_leaves_c_as_it_was()
{
    Rig rig;
    FakeTapeDeck deck;
    rig.start();
    rig.controller->setTapeDeck(&deck);
    mountTape(rig, KD);  // nave.Cas
    deck.result = TapeState::Invalid;
    MenuReply refused = mountTape(rig, KC);
    assert_line(refused, kMainMessage, " CINTA NO VALIDA");
    assert_line(refused, rb::host::kMainTape, tapeLine("NAVE.CAS").c_str());
    deck.result = TapeState::NoMemory;
    refused = mountTape(rig, KC);
    assert_line(refused, kMainMessage, " SIN MEMORIA PARA LA CINTA");
    TEST_ASSERT_EQUAL_STRING("nave.Cas", rig.controller->tapeName());
    TEST_ASSERT_TRUE(rig.events.has("C: name=KARATE.CAS refused: invalid"));
}

// A busy deck or no deck at all: the line says so and C: does not change.
void test_a_busy_or_missing_deck_says_so()
{
    Rig rig;
    FakeTapeDeck deck;
    deck.busy = true;
    rig.start();
    rig.controller->setTapeDeck(&deck);
    assert_line(mountTape(rig, KC), kMainMessage, " CASETERA OCUPADA, REPITE EN UN RATO");
    rig.controller->setTapeDeck(nullptr);
    const MenuReply none = mountTape(rig, KC);
    assert_line(none, kMainMessage, " CASETERA NO DISPONIBLE");
    assert_line(none, rb::host::kMainTape, "C: SIN CINTA");
}

// Esc while the tape is read: the load is dropped and C: does not change.
void test_esc_while_reading_cancels_the_tape()
{
    Rig rig;
    FakeTapeDeck deck;
    deck.pendingFor = 1000;
    rig.start();
    rig.controller->setTapeDeck(&deck);
    rig.press(K1);
    rig.press(KC);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionWait, rig.press(KC).action());
    const MenuReply cancelled = rig.press(KESC);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, cancelled.action());
    assert_line(cancelled, rb::host::kMainTape, "C: SIN CINTA");
    TEST_ASSERT_EQUAL_INT(1, deck.cancels);
    TEST_ASSERT_NULL(rig.controller->tapeName());
}

// AC8: without M3a/H4 the tape mounts, and the line says it cannot load yet.
void test_without_the_gates_the_tape_mounts_with_a_warning()
{
    Rig rig;
    FakeTapeDeck deck;
    deck.gates = false;
    rig.start();
    rig.controller->setTapeDeck(&deck);
    const MenuReply mounted = mountTape(rig, KC);
    assert_line(mounted, rb::host::kMainTape, tapeLine("KARATE.CAS").c_str());
    assert_line(mounted, kMainMessage, " CINTA EN C:, AUN NO PUEDE CARGARSE");
    TEST_ASSERT_TRUE(rig.events.has("C: name=KARATE.CAS mounted gates=0"));
}

// --- T3 (AC5): C: is remembered with D1-D8 -----------------------------------------

// Restores `memory` as menu-sio does at power-up and waits until the drives are back.
void restoreWith(Rig& rig, const rb::host::DriveMemory& memory)
{
    rig.controller->beginRestore();
    rig.controller->restore(memory);
    for (int i = 0; i < 50 && rig.controller->restoring(); ++i) {
        rig.step();
    }
    TEST_ASSERT_FALSE(rig.controller->restoring());
}

rb::host::DriveMemory tapeMemory(const char* tape)
{
    rb::host::DriveMemory memory;
    TEST_ASSERT_TRUE(memory.setTape(tape));
    return memory;
}

// Mounting and taking out a tape are changes to save, like a drive; the snapshot carries C:.
void test_the_tape_on_c_is_in_the_snapshot()
{
    Rig rig;
    FakeTapeDeck deck;
    rig.start();
    rig.controller->setTapeDeck(&deck);
    restoreWith(rig, rb::host::DriveMemory{});
    rig.press(K1);
    rig.press(KC);
    rig.press(KC);    // reading (the deck answers Pending once)
    rig.press(kPoll);  // Ready: C: changes; the screen waits for the save, as for a drive
    TEST_ASSERT_EQUAL_STRING("KARATE.CAS", rig.controller->tapeName());
    TEST_ASSERT_TRUE(rig.controller->memoryChanged());
    rb::host::DriveMemory saved;
    rig.controller->saveResult(rig.controller->snapshot(saved), true, 0);
    TEST_ASSERT_EQUAL_STRING("KARATE.CAS", saved.tape());
    rig.press(KU);
    rig.press(KC);
    TEST_ASSERT_NULL(rig.controller->tapeName());
    TEST_ASSERT_TRUE(rig.controller->memoryChanged());
    rig.controller->snapshot(saved);
    TEST_ASSERT_EQUAL_STRING("", saved.tape());
}

// AC5: after a power cycle the remembered tape is on C: again and the deck is asked to read it.
void test_a_remembered_tape_is_back_on_c_after_power_up()
{
    Rig rig;
    FakeTapeDeck deck;
    rig.start();
    rig.controller->setTapeDeck(&deck);
    restoreWith(rig, tapeMemory("KARATE.CAS"));
    TEST_ASSERT_EQUAL_STRING("KARATE.CAS", rig.controller->tapeName());
    TEST_ASSERT_EQUAL_INT(1, deck.mounts);
    TEST_ASSERT_EQUAL_STRING("KARATE.CAS", deck.last);
    // Review of PR #85: "restored" only once the deck says Ready; until then it is remembered.
    TEST_ASSERT_TRUE(rig.events.has("C: name=KARATE.CAS remembered"));
    TEST_ASSERT_FALSE(rig.events.has("C: name=KARATE.CAS restored"));
    rig.step();
    rig.step();  // the deck answers Ready in the background
    const MenuReply shown = rig.press(kPoll);
    assert_line(shown, rb::host::kMainTape, tapeLine("KARATE.CAS").c_str());
    assert_line(shown, kMainMessage, " UNIDADES RESTAURADAS");
    TEST_ASSERT_FALSE(rig.controller->memoryChanged());  // what was restored is what is saved
    TEST_ASSERT_TRUE(rig.events.has("C: name=KARATE.CAS restored"));
}

// A remembered tape that is no longer on the card: C: empty, and the line says so.
void test_a_missing_remembered_tape_is_reported()
{
    Rig rig;
    FakeTapeDeck deck;
    rig.start();
    rig.controller->setTapeDeck(&deck);
    restoreWith(rig, tapeMemory("BORRADA.CAS"));
    TEST_ASSERT_NULL(rig.controller->tapeName());
    TEST_ASSERT_EQUAL_INT(0, deck.mounts);
    const MenuReply shown = rig.press(kPoll);
    assert_line(shown, rb::host::kMainTape, "C: SIN CINTA");
    assert_line(shown, kMainMessage, " C: FALTA BORRADA.CAS");
}

// A remembered tape the deck cannot read any more: C: is emptied and that is saved.
void test_a_remembered_tape_the_deck_refuses_is_taken_out()
{
    Rig rig;
    FakeTapeDeck deck;
    deck.result = TapeState::Invalid;
    rig.start();
    rig.controller->setTapeDeck(&deck);
    restoreWith(rig, tapeMemory("KARATE.CAS"));
    rig.step();
    rig.step();
    TEST_ASSERT_NULL(rig.controller->tapeName());
    const MenuReply shown = rig.press(kPoll);
    assert_line(shown, rb::host::kMainTape, "C: SIN CINTA");
    TEST_ASSERT_TRUE(rig.events.has("C: name=KARATE.CAS restore refused: invalid"));
    TEST_ASSERT_TRUE(rig.controller->memoryChanged());
    rb::host::DriveMemory saved;
    rig.controller->snapshot(saved);
    TEST_ASSERT_EQUAL_STRING("", saved.tape());
}

// Review of PR #85 (T3): a cassette busy at power-up refuses the first mount(); the remembered
// tape stays on C:, the restore stays pending, and the tape is asked again until the deck takes it.
void test_a_busy_cassette_at_power_up_gets_the_tape_when_it_is_free()
{
    Rig rig;
    FakeTapeDeck deck;
    deck.busy = true;
    rig.start();
    rig.controller->setTapeDeck(&deck);
    restoreWith(rig, tapeMemory("KARATE.CAS"));
    rig.step();
    rig.step();
    TEST_ASSERT_EQUAL_STRING("KARATE.CAS", rig.controller->tapeName());
    TEST_ASSERT_EQUAL_INT(0, deck.accepted);
    TEST_ASSERT_TRUE(deck.mounts >= 1);
    TEST_ASSERT_TRUE(rig.events.has("C: name=KARATE.CAS restore waits: cassette busy"));
    TEST_ASSERT_FALSE(rig.events.has("C: name=KARATE.CAS restored"));
    deck.busy = false;  // the cassette is free: the next service() asks again
    rig.step();
    TEST_ASSERT_EQUAL_INT(1, deck.accepted);
    TEST_ASSERT_EQUAL_STRING("KARATE.CAS", deck.last);
    TEST_ASSERT_FALSE(rig.events.has("C: name=KARATE.CAS restored"));  // accepted is not Ready
    rig.step();
    rig.step();  // Pending once, then Ready
    TEST_ASSERT_EQUAL_STRING("KARATE.CAS", rig.controller->tapeName());
    TEST_ASSERT_TRUE(rig.events.has("C: name=KARATE.CAS restored"));
    const int asked = deck.mounts;
    rig.step();
    TEST_ASSERT_EQUAL_INT(asked, deck.mounts);  // Ready ends the restore: no more requests
    assert_line(rig.press(kPoll), rb::host::kMainTape, tapeLine("KARATE.CAS").c_str());
    TEST_ASSERT_FALSE(rig.controller->memoryChanged());  // C: never changed
}

int main(void)
{
    writeFolder();
    UNITY_BEGIN();
    RUN_TEST(test_the_tapes_are_listed_with_the_disks);
    RUN_TEST(test_a_tape_on_a_drive_says_it_goes_on_c);
    RUN_TEST(test_a_tape_on_d1_says_it_goes_on_c);
    RUN_TEST(test_the_extension_helpers_know_a_tape);
    RUN_TEST(test_c_mounts_the_tape_and_the_c_line_shows_it);
    RUN_TEST(test_a_long_tape_name_is_cut_to_the_line);
    RUN_TEST(test_c_with_a_disk_or_a_xex_says_a_disk_does_not_go_on_c);
    RUN_TEST(test_c_with_a_tnfs_disk_says_a_disk_does_not_go_on_c);
    RUN_TEST(test_u_then_c_takes_the_tape_out);
    RUN_TEST(test_an_invalid_tape_or_no_memory_leaves_c_as_it_was);
    RUN_TEST(test_a_busy_or_missing_deck_says_so);
    RUN_TEST(test_esc_while_reading_cancels_the_tape);
    RUN_TEST(test_without_the_gates_the_tape_mounts_with_a_warning);
    RUN_TEST(test_the_tape_on_c_is_in_the_snapshot);
    RUN_TEST(test_a_remembered_tape_is_back_on_c_after_power_up);
    RUN_TEST(test_a_missing_remembered_tape_is_reported);
    RUN_TEST(test_a_remembered_tape_the_deck_refuses_is_taken_out);
    RUN_TEST(test_a_busy_cassette_at_power_up_gets_the_tape_when_it_is_free);
    return UNITY_END();
}
