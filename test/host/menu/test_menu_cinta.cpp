// Choosing a .cas tape from the menu (spec menu-cinta). T1: the tapes of the local
// folder (RB_TEST_MENU_DIR, written by main() before the tests) are listed with the
// .atr and .xex files, and a drive digit refuses a tape: a tape only goes on C:.

#include <filesystem>
#include <fstream>
#include <string>

#include "menu_tnfs_rig.hpp"

namespace {

void writeFile(const std::string& path, const Bytes& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// A "FUJI" chunk with no data: enough to be listed; T1 does not read the tape.
Bytes tape() { return {'F', 'U', 'J', 'I', 0, 0, 0, 0}; }

// The local folder, in menu order: A DISCO.ATR, B JUEGO.XEX, C KARATE.CAS, D nave.Cas.
void writeFolder()
{
    const std::string dir = RB_TEST_MENU_DIR;
    std::error_code ignored;
    std::filesystem::create_directories(dir, ignored);
    writeFile(dir + "/DISCO.ATR", atr(720, 1));
    writeFile(dir + "/JUEGO.XEX", {0xFF, 0xFF, 0x00, 0x30, 0x00, 0x30, 0x60});
    writeFile(dir + "/KARATE.CAS", tape());
    writeFile(dir + "/nave.Cas", tape());
    writeFile(dir + "/notas.txt", {'x'});
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
    assert_line(list, row(4), "");
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

int main(void)
{
    writeFolder();
    UNITY_BEGIN();
    RUN_TEST(test_the_tapes_are_listed_with_the_disks);
    RUN_TEST(test_a_tape_on_a_drive_says_it_goes_on_c);
    RUN_TEST(test_a_tape_on_d1_says_it_goes_on_c);
    RUN_TEST(test_the_extension_helpers_know_a_tape);
    return UNITY_END();
}
