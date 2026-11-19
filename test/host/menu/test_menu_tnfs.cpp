// TNFS sources in the menu (menu-tnfs T012; AC1 to AC3, AC5, AC6, AC8 to AC11,
// AC13, AC15 to AC21; M41-20): MenuController with a real TnfsLoader over
// FakeTnfsServer, the host repository and its MemoryImageStore, eight drives
// and the mailbox. The server runs on a FakeClock, so "more than 7 s" costs no
// real time; a GatedPort holds chosen requests so a load stays in progress
// while the test presses keys.
//
// Each 'K' is timed in real time (250 ms at most, plan §0). While a load is in
// progress, key() must not allocate on the menu thread (ADR-011): operator new
// is counted per thread, so the loader thread's allocations do not count.

#include "menu_tnfs_rig.hpp"

void setUp() {}
void tearDown() {}

// --- Browsing (AC1, AC2, AC10) ---------------------------------------------------

void test_a_source_opens_its_root_with_folders_first()
{
    Rig rig;
    rig.start();
    const MenuReply root = rig.settle(K2);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, root.action());
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutList, root.layout());
    assert_title(root, " FUENTE 2: /");
    assert_marked(root, row(0), "A APORTES_JUAN_LARRAIN/");  // the cursor (menu-navegacion)
    assert_line(root, row(1), "B JUEGOS/");
    assert_line(root, row(2), "C VACIA/");
    assert_line(root, row(3), "D A.ATR");
    assert_line(root, row(4), "E B.ATR");
    assert_line(root, row(5), "");
    assert_line(root, kListMessage, " PULSA LA LETRA DE LA IMAGEN");
    TEST_ASSERT_TRUE(rig.events.has("menu_open source=2"));
}

void test_a_letter_enters_a_folder_and_esc_climbs_back_to_the_main_screen()
{
    Rig rig;
    rig.start();
    rig.settle(K2);
    const MenuReply empty = rig.settle(KC);
    assert_title(empty, " FUENTE 2: /VACIA");
    assert_line(empty, row(0), "");
    assert_line(empty, kListMessage, " CARPETA VACIA");
    const MenuReply back = rig.settle(KESC);
    assert_title(back, " FUENTE 2: /");
    assert_line(back, row(1), "B JUEGOS/");
    const MenuReply main = rig.settle(KESC);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, main.layout());
    assert_line(main, kMainMessage, "");
    // Opening the source again starts at its root.
    rig.walk({K2, KB});
    assert_title(rig.settle(KESC), " FUENTE 2: /");
    rig.settle(KESC);
    assert_title(rig.settle(K2), " FUENTE 2: /");
}

void test_more_than_twenty_entries_show_page_1_and_the_total()
{
    Rig rig;
    char name[32];
    for (int i = 0; i < 25; ++i) {
        std::snprintf(name, sizeof name, "/Muchas/n%02d.atr", i);
        rig.server.addFile(name, Bytes(16, 0));
    }
    rig.start();
    rig.settle(K2);
    const MenuReply muchas = rig.settle(KC);  // APORTES_JUAN_LARRAIN/, JUEGOS/, MUCHAS/
    assert_title(muchas, " FUENTE 2: /MUCHAS");
    assert_marked(muchas, row(0), "A N00.ATR");
    assert_line(muchas, row(19), "T N19.ATR");
    assert_line(muchas, kListMessage, " PAGINA 1 DE 2 (25)");  // menu-navegacion: was MOSTRANDO 20 DE 25
}

// --- Keys (plan §0) ----------------------------------------------------------------

void test_the_poll_code_never_chooses_and_3e_chooses_s()
{
    Rig rig;
    char name[32];
    for (char c = 'a'; c <= 't'; ++c) {
        std::snprintf(name, sizeof name, "/Letras/%c.atr", c);
        rig.server.addFile(name, atr(720, static_cast<uint8_t>(c)));
    }
    rig.start();
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, rig.press(kPoll).layout());  // not a key
    rig.walk({K2, KC});                                                              // LETRAS/
    const MenuReply polled = rig.press(kPoll);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, polled.action());
    assert_line(polled, kListMessage, " PULSA LA LETRA DE LA IMAGEN");
    assert_line(rig.press(KS), kMainMessage, " UNIDAD PARA: S.ATR");  // menu-navegacion M42-4
    assert_line(rig.press(KESC), kListMessage, " PULSA LA LETRA DE LA IMAGEN");
    // Shift and Control are still ignored: $7E is S.
    assert_line(rig.press(KS | 0x40), kMainMessage, " UNIDAD PARA: S.ATR");
}

// menu-editar-servidor: a saved server is used by the next opening, without a restart.
void test_a_server_saved_with_e_is_the_one_the_next_opening_uses()
{
    constexpr const char* file = RB_TEST_MENU_DIR "/sources-tnfs-edit.txt";
    std::FILE* out = std::fopen(file, "wb");
    TEST_ASSERT_NOT_NULL(out);
    std::fputs("pulgarcito\nno-such-host\nsin-enlace\n", out);
    std::fclose(out);
    Rig rig;
    rig.start();
    rig.controller->setSourcesFile(file);
    assert_title(rig.settle(K2), " FUENTE 2: /");  // the first session, with the old name
    rig.settle(KESC);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionEdit, rig.controller->key(KE, 1).action());
    const char* name = "10.0.0.7";
    for (uint8_t i = 0; i < rb::host::kMenuEditWidth; ++i) {
        rig.controller->text(i, static_cast<uint8_t>(i < std::strlen(name) ? name[i] : ' '));
    }
    assert_line(rig.controller->key(rb::host::kMenuEditSave), kMainMessage, " FUENTE 2 GUARDADA");
    // Opening source 2 again: the loader renamed its link first, then opened it.
    assert_title(rig.settle(K2), " FUENTE 2: /");
    TEST_ASSERT_EQUAL_INT(1, rig.link.renames.load());
    TEST_ASSERT_EQUAL_STRING("10.0.0.7", rig.link.renamedTo);
    TEST_ASSERT_EQUAL_INT(0, rig.unresolved.renames.load());
    std::remove(file);
}

void test_e_on_source_1_says_it_is_local_and_source_1_is_unchanged()
{
    Rig rig;
    rig.start();
    assert_line(rig.press(KE), kMainMessage, " LA FUENTE 1 ES LOCAL");
    const MenuReply list = rig.press(K1);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutList, list.layout());
    assert_title(list, " FUENTE 1: SD");
    assert_marked(list, row(0), " A  DEMO-DOS.ATR");
    assert_line(list, kListMessage, " PULSA LA LETRA DE LA IMAGEN");
    rig.press(KB);
    const MenuReply d2 = rig.press(K2);
    assert_line(d2, driveLine(2), "D2 DEMO-UNO.ATR");
    rig.step();
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.mounted(2));
}

// --- Images (AC3, AC4, AC8, AC15) --------------------------------------------------

void test_an_atr_chosen_for_d1_is_assigned_with_its_display_name_and_boots()
{
    Rig rig;
    rig.start();
    rig.walk({K2, KA, KA});  // APORTES_JUAN_LARRAIN/, 0- PRUEBA/
    const MenuReply pick = rig.press(KA);
    assert_line(pick, kMainMessage, " UNIDAD PARA: ATARI LOGO.ATR");  // menu-navegacion M42-4
    const MenuReply assigned = rig.settle(K1);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, assigned.layout());
    assert_line(assigned, driveLine(1), "D1 ATARI LOGO.ATR");
    assert_line(assigned, kMainMessage, " D1: ATARI LOGO.ATR (A ARRANCA)");
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());
    TEST_ASSERT_EQUAL_STRING("ATARI LOGO.ATR", rig.controller->driveName(1));
    TEST_ASSERT_EQUAL_size_t(1, slotsInUse(rig.store()));
    // The SHA-256 of the image goes to the host log.
    const Bytes logo = rig.server.fileBytes(kLogoPath);
    const rb::host::Sha256Digest digest = rb::host::sha256(Span<const uint8_t>(logo.data(), logo.size()));
    char hex[65];
    for (int i = 0; i < 32; ++i) {
        std::snprintf(hex + 2 * i, 3, "%02x", digest[static_cast<std::size_t>(i)]);
    }
    TEST_ASSERT_TRUE(rig.events.has(hex));

    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionBoot, rig.press(KA).action());
    TEST_ASSERT_FALSE(rig.controller->menuOnD1());
    TEST_ASSERT_EQUAL_STRING_LEN("*tnfs*", rig.mounted(1), 6);
    // A cold reset brings the menu back; D1 keeps its image for the next A.
    rig.controller->onColdReset();
    rig.controller->service();
    rig.step();
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());
    assert_line(rig.press(kRedraw), driveLine(1), "D1 ATARI LOGO.ATR");
    TEST_ASSERT_EQUAL_size_t(1, slotsInUse(rig.store()));
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionBoot, rig.press(KA).action());
}

void test_a_load_longer_than_seven_seconds_shows_its_progress_and_mounts_on_d2()
{
    Rig rig;
    rig.server.delay(tnfs::cmd::kRead, 100);  // 180 READs: 18 s on the fake clock
    rig.start();
    rig.walk({K2, KA, KA});
    rig.press(KA);
    rig.gated.gate(tnfs::cmd::kRead);
    const MenuReply first = rig.press(K2);
    assert_loading(first, kListMessage, "0");
    assert_title(first, " FUENTE 2: ..RTES_JUAN_LARRAIN/0- PRUEBA");  // 40 columns, cut on the left (N008)
    rig.gated.awaitHeld();
    unsigned last = 0;
    bool rose = false;
    for (int batch = 0; batch < 4; ++batch) {
        rig.gated.permit(40);
        for (int i = 0; i < 40; ++i) {
            rig.gated.awaitHeld();
        }
        const MenuReply loading = rig.press(kPoll);
        TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionWait, loading.action());
        const unsigned percent = percentOf(loading, kListMessage);
        TEST_ASSERT_TRUE(percent >= last);
        TEST_ASSERT_TRUE(percent <= 99);
        rose = rose || percent > last;
        last = percent;
    }
    TEST_ASSERT_TRUE(rose);
    // A key other than Esc is ignored while loading.
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionWait, rig.press(KB).action());
    rig.gated.open();
    MenuReply done = rig.press(kPoll);
    for (int i = 0; i < 200 && done.action() == rb::host::kMenuActionWait; ++i) {
        done = rig.press(kPoll);
    }
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, done.action());
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, done.layout());
    assert_line(done, driveLine(2), "D2 ATARI LOGO.ATR");
    assert_line(done, kMainMessage, " D2: ATARI LOGO.ATR");
    TEST_ASSERT_EQUAL_STRING_LEN("*tnfs*", rig.mounted(2), 6);
    TEST_ASSERT_TRUE(fakeClock.nowMicros() > 7000000u);
}

void test_an_invalid_atr_is_refused_for_d3_and_its_slot_is_freed()
{
    Rig rig;
    rig.start();
    rig.walk({K2, KB});  // JUEGOS/
    rig.press(KA);       // ROTA.ATR
    const MenuReply refused = rig.settle(K3);
    assert_line(refused, kListMessage, " ERROR: ATR INVALIDA");
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutList, refused.layout());
    TEST_ASSERT_TRUE(rig.mailbox.idle());
    TEST_ASSERT_EQUAL_STRING("", rig.mounted(3));
    TEST_ASSERT_NULL(rig.controller->driveName(3));
    awaitSlots(rig.store(), 0);
}

// --- Errors (AC5, AC6, AC7, AC16, AC20, AC21, no memory) ------------------------

void test_a_silent_server_says_so_and_d1_keeps_its_image()
{
    Rig rig;
    for (int i = 0; i < 3; ++i) {
        rig.server.silence(tnfs::cmd::kMount);
    }
    rig.start();
    rig.press(K1);
    rig.press(KA);
    rig.press(K1);  // DEMO-DOS.ATR assigned to D1
    const MenuReply silent = rig.settle(K2);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, silent.layout());
    assert_line(silent, kMainMessage, " ERROR: SERVIDOR NO RESPONDE");
    assert_line(silent, driveLine(1), "D1 DEMO-DOS.ATR");
    TEST_ASSERT_TRUE(rig.events.has("menu_tnfs"));
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionBoot, rig.press(KA).action());
    TEST_ASSERT_EQUAL_STRING("demo-dos.atr", rig.mounted(1));
}

void test_an_unresolved_name_says_not_found_and_an_unlinked_source_too()
{
    Rig rig;
    rig.start();
    assert_line(rig.settle(K3), kMainMessage, " ERROR: SERVIDOR NO ENCONTRADO");
    assert_line(rig.settle(K4), kMainMessage, " ERROR: SERVIDOR NO ENCONTRADO");
    assert_line(rig.settle(K5), kMainMessage, " FUENTE 5 SIN CONFIGURAR");
}

void test_a_refused_folder_keeps_the_view_where_it_was()
{
    Rig rig;
    rig.start();
    rig.settle(K2);
    rig.server.reject(tnfs::cmd::kOpenDir, FakeTnfsServer::kNoEntry);
    const MenuReply refused = rig.settle(KB);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutList, refused.layout());
    assert_title(refused, " FUENTE 2: /");
    assert_marked(refused, row(1), "B JUEGOS/");  // the row chosen keeps the cursor
    assert_line(refused, kListMessage, " ERROR: NO SE PUDO ABRIR");
}

void test_malformed_replies_read_as_a_silent_server_and_the_drives_go_on()
{
    Rig rig;
    rig.start();
    rig.press(K1);
    rig.press(KB);
    rig.press(K2);  // DEMO-UNO.ATR on D2
    for (int i = 0; i < 3; ++i) {
        rig.server.malformed(tnfs::cmd::kMount);
    }
    const MenuReply bad = rig.settle(K2);
    assert_line(bad, kMainMessage, " ERROR: SERVIDOR NO RESPONDE");
    assert_line(bad, driveLine(2), "D2 DEMO-UNO.ATR");
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.mounted(2));
}

void test_a_server_that_dies_while_loading_changes_no_drive()
{
    Rig rig;
    rig.start();
    rig.press(K1);
    rig.press(KB);
    rig.press(K2);  // DEMO-UNO.ATR on D2
    rig.walk({K2, KA, KA});
    rig.press(KA);
    for (int i = 0; i < 3; ++i) {
        rig.server.silence(tnfs::cmd::kRead);
    }
    const MenuReply dead = rig.settle(K2);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutList, dead.layout());
    assert_line(dead, kListMessage, " ERROR: SERVIDOR NO RESPONDE");
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.mounted(2));
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.controller->driveName(2));  // the catalog name
    awaitSlots(rig.store(), 0);
}

void test_no_memory_for_an_image_says_it_could_not_open_it()
{
    Rig rig;
    rig.start();
    rig.walk({K2, KA, KA});
    rig.press(KA);
    rig.memory.fail = true;
    const MenuReply refused = rig.settle(K2);
    assert_line(refused, kListMessage, " ERROR: NO SE PUDO ABRIR");
    TEST_ASSERT_TRUE(rig.events.has("sin memoria"));
    TEST_ASSERT_EQUAL_STRING("", rig.mounted(2));
}

// --- Slow listing, cancel and late replies (AC17, AC18, AC19) ------------------------

void test_a_slow_listing_shows_its_progress_then_the_folder()
{
    Rig rig;
    rig.start();
    rig.gated.gate(tnfs::cmd::kStat);
    const MenuReply first = rig.press(K2);
    assert_loading(first, kMainMessage, "0");
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, first.layout());  // the view stays where it was
    rig.gated.awaitHeld();
    rig.gated.open();
    MenuReply done = rig.press(kPoll);
    for (int i = 0; i < 200 && done.action() == rb::host::kMenuActionWait; ++i) {
        done = rig.press(kPoll);
    }
    assert_title(done, " FUENTE 2: /");
    assert_marked(done, row(0), "A APORTES_JUAN_LARRAIN/");
}

void test_esc_cancels_a_load_and_its_late_reply_changes_nothing()
{
    Rig rig;
    rig.start();
    rig.press(K1);
    rig.press(KB);
    rig.press(K2);  // DEMO-UNO.ATR on D2
    rig.walk({K2, KA, KA});
    rig.press(KA);
    rig.gated.gate(tnfs::cmd::kRead);
    assert_loading(rig.press(K2), kListMessage, "0");
    rig.gated.awaitHeld();
    const MenuReply cancelled = rig.press(KESC);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, cancelled.action());
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutList, cancelled.layout());
    assert_line(cancelled, kListMessage, " PULSA LA LETRA DE LA IMAGEN");
    rig.gated.open();  // the server answers late: the result is dropped
    awaitSlots(rig.store(), 0);
    const MenuReply after = rig.press(kPoll);
    assert_line(after, kListMessage, " PULSA LA LETRA DE LA IMAGEN");
    rig.settle(KESC);
    rig.settle(KESC);
    const MenuReply main = rig.settle(KESC);
    assert_line(main, driveLine(2), "D2 DEMO-UNO.ATR");
    assert_line(main, kMainMessage, "");
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.mounted(2));
}

void test_a_cold_reset_during_a_load_cancels_it()
{
    Rig rig;
    rig.start();
    rig.walk({K2, KA, KA});
    rig.press(KA);
    rig.gated.gate(tnfs::cmd::kRead);
    rig.press(K1);
    rig.gated.awaitHeld();
    rig.controller->onColdReset();
    rig.controller->service();
    rig.gated.open();
    awaitSlots(rig.store(), 0);
    const MenuReply main = rig.press(kRedraw);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, main.action());
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, main.layout());
    assert_line(main, driveLine(1), "D1 SIN DISCO");
}

// --- Slots go back to the loader (M41-20) -------------------------------------------

void test_slots_the_menu_stops_using_are_freed()
{
    Rig rig;
    rig.start();
    rig.walk({K2, KA, KA});
    rig.press(KA);
    rig.settle(K1);  // D1: slot a
    rig.walk({K2, KA, KA});
    rig.press(KA);
    rig.settle(K1);  // D1 again: slot a goes back
    awaitSlots(rig.store(), 1);
    rig.walk({K2, KA, KA});
    rig.press(KA);
    rig.settle(K2);  // D2: one more
    rig.step();
    awaitSlots(rig.store(), 2);
    rig.walk({K2, KA, KA});
    rig.press(KA);
    rig.settle(K2);  // D2 again: the displaced slot goes back once the reply is collected
    rig.step();
    awaitSlots(rig.store(), 2);
    rig.press(KU);
    rig.press(K2);  // D2 unmounted
    rig.step();
    awaitSlots(rig.store(), 1);
    rig.press(KU);
    rig.press(K1);  // D1 unassigned
    awaitSlots(rig.store(), 0);
    TEST_ASSERT_EQUAL_STRING("", rig.mounted(2));
}

void test_local_images_replace_tnfs_ones_and_free_their_slots()
{
    Rig rig;
    rig.start();
    rig.walk({K2, KA, KA});
    rig.press(KA);
    rig.settle(K1);
    rig.walk({K2, KA, KA});
    rig.press(KA);
    rig.settle(K2);
    rig.step();
    awaitSlots(rig.store(), 2);
    rig.press(K1);
    rig.press(KA);
    rig.press(K1);  // DEMO-DOS.ATR on D1
    rig.press(K1);
    rig.press(KB);
    rig.press(K2);  // DEMO-UNO.ATR on D2
    rig.step();
    awaitSlots(rig.store(), 0);
    const MenuReply main = rig.press(kRedraw);
    assert_line(main, driveLine(1), "D1 DEMO-DOS.ATR");
    assert_line(main, driveLine(2), "D2 DEMO-UNO.ATR");
}

// --- Mailbox and cold reset around TNFS images (review L10) ----------------------------

void test_a_fetch_that_ends_with_the_mailbox_busy_is_refused_and_freed()
{
    Rig rig;
    rig.start();
    rig.walk({K2, KA, KA});
    rig.press(KA);
    rig.gated.gate(tnfs::cmd::kRead);
    assert_loading(rig.press(K2), kListMessage, "0");
    rig.gated.awaitHeld();
    // Another request takes the mailbox before the image arrives.
    rb::app::UnmountDiskImage other(rig.mailbox);
    TEST_ASSERT_TRUE(other.execute(DriveNumber::from(5).value()).ok());
    rig.gated.open();
    MenuReply done = rig.controller->key(kPoll);  // no step: the mailbox stays busy
    for (int i = 0; i < 200 && done.action() == rb::host::kMenuActionWait; ++i) {
        done = rig.controller->key(kPoll);
    }
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutList, done.layout());
    assert_line(done, kListMessage, " ERROR: UNIDAD OCUPADA, REPITE");
    TEST_ASSERT_NULL(rig.controller->driveName(2));
    awaitSlots(rig.store(), 0);
    rig.step();
    TEST_ASSERT_EQUAL_STRING("", rig.mounted(2));
}

void test_a_tnfs_image_on_d1_boots_again_after_a_cold_reset()
{
    Rig rig;
    rig.start();
    rig.walk({K2, KA, KA});
    rig.press(KA);
    rig.settle(K1);
    for (int round = 0; round < 2; ++round) {
        const MenuReply boot = rig.controller->key(KA);
        TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionBoot, boot.action());
        rig.step();
        TEST_ASSERT_EQUAL_STRING_LEN("*tnfs*", rig.mounted(1), 6);
        TEST_ASSERT_FALSE(rig.controller->menuOnD1());
        rig.controller->onColdReset();
        rig.controller->service();
        rig.step();
        TEST_ASSERT_TRUE(rig.controller->menuOnD1());
        TEST_ASSERT_EQUAL_size_t(1, slotsInUse(rig.store()));
    }
    assert_line(rig.press(kRedraw), driveLine(1), "D1 ATARI LOGO.ATR");
}

void test_a_cold_reset_with_a_tnfs_mount_in_flight_keeps_the_slots_right()
{
    Rig rig;
    rig.start();
    rig.walk({K2, KA, KA});
    rig.press(KA);
    rig.settle(K2);  // D2: slot a, mounted
    awaitSlots(rig.store(), 1);
    rig.walk({K2, KA, KA});
    rig.press(KA);
    MenuReply second = rig.controller->key(K2);  // no step from here: the mount stays in flight
    for (int i = 0; i < 200 && second.action() == rb::host::kMenuActionWait; ++i) {
        second = rig.controller->key(kPoll);
    }
    assert_line(second, driveLine(2), "D2 ATARI LOGO.ATR");
    TEST_ASSERT_FALSE(rig.mailbox.idle());
    rig.controller->onColdReset();
    rig.controller->service();  // the mailbox is busy: the menu waits
    TEST_ASSERT_TRUE(rig.controller->coldResetPending());
    rig.step();  // the D2 mount is applied and collected; the menu mount is queued
    rig.step();  // the menu is back on D1
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());
    TEST_ASSERT_EQUAL_STRING_LEN("*tnfs*", rig.mounted(2), 6);
    awaitSlots(rig.store(), 1);  // slot a went back; the new one is on D2
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_a_source_opens_its_root_with_folders_first);
    RUN_TEST(test_a_letter_enters_a_folder_and_esc_climbs_back_to_the_main_screen);
    RUN_TEST(test_more_than_twenty_entries_show_page_1_and_the_total);
    RUN_TEST(test_the_poll_code_never_chooses_and_3e_chooses_s);
    RUN_TEST(test_e_on_source_1_says_it_is_local_and_source_1_is_unchanged);
    RUN_TEST(test_a_server_saved_with_e_is_the_one_the_next_opening_uses);
    RUN_TEST(test_an_atr_chosen_for_d1_is_assigned_with_its_display_name_and_boots);
    RUN_TEST(test_a_load_longer_than_seven_seconds_shows_its_progress_and_mounts_on_d2);
    RUN_TEST(test_an_invalid_atr_is_refused_for_d3_and_its_slot_is_freed);
    RUN_TEST(test_a_silent_server_says_so_and_d1_keeps_its_image);
    RUN_TEST(test_an_unresolved_name_says_not_found_and_an_unlinked_source_too);
    RUN_TEST(test_a_refused_folder_keeps_the_view_where_it_was);
    RUN_TEST(test_malformed_replies_read_as_a_silent_server_and_the_drives_go_on);
    RUN_TEST(test_a_server_that_dies_while_loading_changes_no_drive);
    RUN_TEST(test_no_memory_for_an_image_says_it_could_not_open_it);
    RUN_TEST(test_a_slow_listing_shows_its_progress_then_the_folder);
    RUN_TEST(test_esc_cancels_a_load_and_its_late_reply_changes_nothing);
    RUN_TEST(test_a_cold_reset_during_a_load_cancels_it);
    RUN_TEST(test_slots_the_menu_stops_using_are_freed);
    RUN_TEST(test_local_images_replace_tnfs_ones_and_free_their_slots);
    RUN_TEST(test_a_fetch_that_ends_with_the_mailbox_busy_is_refused_and_freed);
    RUN_TEST(test_a_tnfs_image_on_d1_boots_again_after_a_cold_reset);
    RUN_TEST(test_a_cold_reset_with_a_tnfs_mount_in_flight_keeps_the_slots_right);
    return UNITY_END();
}
