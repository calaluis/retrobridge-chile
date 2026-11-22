// Break in the menu (menu-unidades-persistentes T5, C7): from the list, the drive choice,
// any TNFS folder (three levels deep) and the drive choice of a folder, Break goes to the
// main screen and changes no drive. On the main screen, in Unmount, Edit and Create it does
// nothing (spec: suposición tomada). Rig of test_menu_tnfs: MenuController with a real
// TnfsLoader over FakeTnfsServer and the host repository.

#include "menu_tnfs_rig.hpp"

void setUp() {}
void tearDown() {}

namespace {

constexpr uint8_t KBREAK = rb::host::kMenuBreakKey;
constexpr uint8_t KN = 0x23;

// The drives as the main screen draws them, and as the bay mounts them.
struct Drives {
    std::array<std::string, 8> mounted;
    std::array<std::array<uint8_t, 40>, 8> lines;
};

Drives drives(Rig& rig, const MenuReply& main)
{
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, main.layout());
    Drives out;
    for (unsigned n = 1; n <= 8; ++n) {
        out.mounted[n - 1] = rig.mounted(n);
        std::memcpy(out.lines[n - 1].data(), main.screen() + driveLine(n), 40);
    }
    return out;
}

void assert_same(const Drives& before, const Drives& after)
{
    for (std::size_t i = 0; i < 8; ++i) {
        TEST_ASSERT_EQUAL_STRING(before.mounted[i].c_str(), after.mounted[i].c_str());
        TEST_ASSERT_EQUAL_UINT8_ARRAY(before.lines[i].data(), after.lines[i].data(), 40);
    }
}

// D2 with DEMO-UNO.ATR from the local list, D1 with the menu: the drives Break must keep.
Drives withD2(Rig& rig)
{
    rig.settle(K1);
    rig.press(KB);
    const MenuReply main = rig.settle(K2);
    assert_line(main, driveLine(2), "D2 DEMO-UNO.ATR");
    return drives(rig, main);
}

void assert_main_after_break(Rig& rig, const Drives& before)
{
    const MenuReply main = rig.press(KBREAK);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, main.action());
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, main.layout());
    assert_line(main, kMainMessage, "");
    assert_same(before, drives(rig, main));
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());
}

}  // namespace

void test_break_from_the_list_goes_to_the_main_screen_and_keeps_the_drives()
{
    Rig rig;
    rig.start();
    const Drives before = withD2(rig);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutList, rig.settle(K1).layout());
    assert_main_after_break(rig, before);
}

void test_break_while_choosing_the_drive_assigns_nothing()
{
    Rig rig;
    rig.start();
    const Drives before = withD2(rig);
    rig.settle(K1);
    assert_line(rig.press(KA), kMainMessage, " UNIDAD PARA: DEMO-DOS.ATR");  // the Pick view
    assert_main_after_break(rig, before);
    // The image chosen before Break is forgotten: a digit now is a source, not a drive.
    const MenuReply list = rig.settle(K1);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutList, list.layout());
    assert_line(list, kListMessage, " PULSA LA LETRA DE LA IMAGEN");
    assert_same(before, drives(rig, rig.press(KBREAK)));
}

void test_break_three_folders_deep_goes_to_the_main_screen_and_the_next_opening_starts_at_the_root()
{
    Rig rig;
    rig.server.addFile("/Juegos/Dos/Tres/c.atr", atr(720, 4));
    rig.start();
    const Drives before = withD2(rig);
    rig.walk({K2, KB, KA, KA});  // /Juegos, /Juegos/Dos, /Juegos/Dos/Tres
    assert_title(rig.shown, " FUENTE 2: /JUEGOS/DOS/TRES");
    assert_marked(rig.shown, row(0), "A C.ATR");
    assert_main_after_break(rig, before);
    assert_title(rig.settle(K2), " FUENTE 2: /");
    // One Esc from the root is the main screen: no folder of the old stack is left.
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, rig.settle(KESC).layout());
}

void test_break_while_choosing_the_drive_of_a_folder_image_fetches_nothing()
{
    Rig rig;
    rig.server.addFile("/Juegos/Dos/Tres/c.atr", atr(720, 4));
    rig.start();
    const Drives before = withD2(rig);
    rig.walk({K2, KB, KA, KA});
    assert_line(rig.press(KA), kMainMessage, " UNIDAD PARA: C.ATR");  // FolderPick
    assert_main_after_break(rig, before);
    TEST_ASSERT_EQUAL_size_t(0, slotsInUse(rig.store()));  // no image was fetched
}

void test_break_while_a_folder_loads_cancels_it_and_its_late_reply_changes_nothing()
{
    Rig rig;
    rig.start();
    const Drives before = withD2(rig);
    rig.settle(K2);
    rig.gated.gate(tnfs::cmd::kReadDir);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionWait, rig.press(KB).action());  // /Juegos loads
    rig.gated.awaitHeld();
    assert_main_after_break(rig, before);
    TEST_ASSERT_TRUE(rig.events.has("load cancelled with Break"));
    rig.gated.open();  // the server answers late: the result is dropped
    const MenuReply after = rig.press(kPoll);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, after.layout());
    assert_same(before, drives(rig, after));
}

void test_break_on_the_main_screen_and_in_unmount_changes_nothing()
{
    Rig rig;
    rig.start();
    const Drives before = withD2(rig);
    const MenuReply main = rig.press(kRedraw);
    const MenuReply same = rig.press(KBREAK);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(main.bytes.data(), same.bytes.data(), main.bytes.size());
    const MenuReply unmount = rig.press(KU);
    const MenuReply still = rig.press(KBREAK);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(unmount.bytes.data(), still.bytes.data(), unmount.bytes.size());
    rig.press(KESC);
    assert_same(before, drives(rig, rig.press(kRedraw)));
}

void test_break_while_editing_a_server_or_naming_a_new_atr_changes_nothing()
{
    constexpr const char* file = RB_TEST_MENU_DIR "/sources-break-edit.txt";
    std::FILE* out = std::fopen(file, "wb");
    TEST_ASSERT_NOT_NULL(out);
    std::fputs("pulgarcito\nno-such-host\nsin-enlace\n", out);
    std::fclose(out);
    Rig rig;
    rig.start();
    rig.controller->setSourcesFile(file);
    const MenuReply edit = rig.controller->key(KE, 1);  // E on source 2 (a server)
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionEdit, edit.action());
    const MenuReply still = rig.controller->key(KBREAK, rb::host::kMenuNoRow);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(edit.bytes.data(), still.bytes.data(), edit.bytes.size());
    rig.controller->key(KESC, rb::host::kMenuNoRow);
    const MenuReply create = rig.controller->key(KN, rb::host::kMenuNoRow);
    const MenuReply same = rig.controller->key(KBREAK, rb::host::kMenuNoRow);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(create.bytes.data(), same.bytes.data(), create.bytes.size());
    std::remove(file);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_break_from_the_list_goes_to_the_main_screen_and_keeps_the_drives);
    RUN_TEST(test_break_while_choosing_the_drive_assigns_nothing);
    RUN_TEST(test_break_three_folders_deep_goes_to_the_main_screen_and_the_next_opening_starts_at_the_root);
    RUN_TEST(test_break_while_choosing_the_drive_of_a_folder_image_fetches_nothing);
    RUN_TEST(test_break_while_a_folder_loads_cancels_it_and_its_late_reply_changes_nothing);
    RUN_TEST(test_break_on_the_main_screen_and_in_unmount_changes_nothing);
    RUN_TEST(test_break_while_editing_a_server_or_naming_a_new_atr_changes_nothing);
    return UNITY_END();
}
