// MenuController with persistent drives (menu-unidades-persistentes T3; C1, C3, C4, C5, C8, C12):
// the controller says what D1..D8 hold after every applied change (snapshot), shows the save as
// GUARDANDO / GUARDADO / NO SE GUARDO, and at power-up restores what was remembered behind the
// RESTAURANDO UNIDADES screen, with A disabled until it ends. The store itself is test_drive_slot_store.

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <optional>

#include "rb/menu/drive_memory.hpp"
#include "rb/menu/host_image_repository.hpp"
#include "rb/menu/menu_catalog.hpp"
#include "rb/menu/menu_controller.hpp"
#include "rb/menu/menu_image.hpp"
#include "rb/menu/menu_sources.hpp"
#include "rb/app/drive_bay.hpp"
#include "rb/app/inline_drive_mailbox.hpp"
#include "rb/app/usecases/apply_drive_request.hpp"
#include "rb/domain/disk_image.hpp"
#include "unity.h"

#ifndef RB_TEST_MENU_DIR
#error "RB_TEST_MENU_DIR must hold the images of make_atr.py --demo"
#endif

using rb::core::Span;
using rb::domain::DriveNumber;
using rb::host::DriveMemory;
using rb::host::MenuReply;

namespace {

// Atari keyboard codes. The demo folder lists demo-dos.atr (A), demo-uno.atr (B), rota.atr (C).
constexpr uint8_t K1 = 0x1F, K2 = 0x1E, K3 = 0x1A, KA = 0x3F, KB = 0x15, KU = 0x0B;
constexpr uint8_t kNoKey = static_cast<uint8_t>(rb::host::kMenuNoKey);
constexpr uint8_t kPoll = static_cast<uint8_t>(rb::host::kMenuPollKey);

DriveNumber drive(unsigned n) { return DriveNumber::from(static_cast<uint8_t>(n)).value(); }

std::array<uint8_t, 40> line(const char* text)
{
    std::array<uint8_t, 40> out{};
    rb::host::screenLine(text, Span<uint8_t>{out});
    return out;
}

void assert_line(const MenuReply& reply, unsigned offset, const char* text)
{
    const auto expected = line(text);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected.data(), reply.screen() + offset, 40, text);
}

unsigned driveLine(unsigned n) { return rb::host::kMainDrives + (n - 1) * 40; }

DriveMemory memoryOf(std::initializer_list<const char*> names)
{
    DriveMemory memory;
    unsigned n = 1;
    for (const char* name : names) {
        TEST_ASSERT_TRUE(memory.set(n++, name));
    }
    return memory;
}

// The menu-sio wiring minus SIO: the demo folder, eight drives and the controller. As in
// start(): the menu goes on D1 and then the controller restores (beginRestore).
struct Rig {
    rb::host::MenuCatalog catalog;
    rb::host::MenuSources sources;
    rb::host::MenuImage menu;
    std::optional<rb::host::HostImageRepository> repository;
    rb::app::DriveBay bay{8};
    rb::app::InlineDriveMailbox mailbox;
    rb::app::ApplyDriveRequest apply{bay, mailbox};
    std::optional<rb::host::MenuController> controller;

    // withCatalog=false: as on the FujiNet, D1 serves the menu before the microSD catalog is read.
    explicit Rig(bool restoring = true, bool withCatalog = true)
    {
        if (withCatalog) {
            loadCatalog();
        }
        repository.emplace(RB_TEST_MENU_DIR, menu);
        controller.emplace(catalog, sources, *repository, mailbox, nullptr);
        controller->onColdReset();
        controller->service();
        step();
        if (restoring) {
            controller->beginRestore();
        }
    }

    // Restored with `memory` and every queued mount applied.
    void restored(const DriveMemory& memory = DriveMemory{})
    {
        controller->restore(memory);
        for (int i = 0; i < 20 && controller->restoring(); ++i) {
            step();
        }
        TEST_ASSERT_FALSE(controller->restoring());
    }

    void loadCatalog() { TEST_ASSERT_TRUE(rb::host::readCatalog(RB_TEST_MENU_DIR, catalog)); }

    void step()
    {
        apply.execute();
        controller->service();
    }

    MenuReply press(uint8_t code) { return controller->key(code); }

    MenuReply keys(std::initializer_list<uint8_t> codes)
    {
        MenuReply last;
        for (uint8_t code : codes) {
            last = press(code);
            step();
        }
        return last;
    }

    const char* mounted(unsigned n) const
    {
        const auto* location = bay.mountedLocation(drive(n));
        return location != nullptr ? location->c_str() : "";
    }

    // The memory the save task would get now, and its version.
    uint32_t take(DriveMemory& out)
    {
        TEST_ASSERT_TRUE(controller->memoryChanged());
        const uint32_t version = controller->snapshot(out);
        TEST_ASSERT_FALSE(controller->memoryChanged());
        return version;
    }
};

}  // namespace

void setUp() {}
void tearDown() {}

// --- Restoring at power-up -----------------------------------------------------

void test_restoring_shows_its_screen_and_ignores_every_key_even_a()
{
    Rig rig;
    for (uint8_t code : {kNoKey, kPoll, K1, KA, KU}) {
        const MenuReply reply = rig.press(code);
        TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionWait, reply.action());  // the Atari polls
        TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, reply.layout());
        assert_line(reply, rb::host::kMainMessage, " RESTAURANDO UNIDADES");
    }
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());
    TEST_ASSERT_TRUE(rig.mailbox.idle());
}

void test_a_cold_reset_while_restoring_keeps_the_restoring_screen()
{
    Rig rig;
    rig.controller->onColdReset();
    rig.step();
    assert_line(rig.press(kNoKey), rb::host::kMainMessage, " RESTAURANDO UNIDADES");
    rig.restored();
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, rig.press(kNoKey).action());
}

// Waiting for the read is not applying it: service() before restore() mounts nothing, even
// with an empty catalog (entry 0 is a valid index), and the restore is not finished early.
void test_service_before_restore_waits_and_mounts_nothing()
{
    Rig rig(true, false);
    for (int i = 0; i < 10; ++i) {
        rig.step();
        TEST_ASSERT_TRUE(rig.controller->restoring());
    }
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());
    TEST_ASSERT_EQUAL_STRING(rb::host::kMenuLocation, rig.mounted(1));
    for (unsigned n = 2; n <= 8; ++n) {
        TEST_ASSERT_EQUAL_STRING("", rig.mounted(n));
    }
    for (uint8_t code : {kNoKey, KA}) {
        const MenuReply reply = rig.press(code);
        TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionWait, reply.action());  // A stays disabled
        assert_line(reply, rb::host::kMainMessage, " RESTAURANDO UNIDADES");
    }
    rig.step();
    TEST_ASSERT_EQUAL_STRING("", rig.mounted(2));

    rig.loadCatalog();
    rig.restored(memoryOf({"demo-dos.atr", "demo-uno.atr", "", "demo-dos.atr"}));
    const MenuReply main = rig.press(kNoKey);
    assert_line(main, driveLine(1), "D1 DEMO-DOS.ATR");
    assert_line(main, rb::host::kMainMessage, " UNIDADES RESTAURADAS");
    TEST_ASSERT_EQUAL_STRING(rb::host::kMenuLocation, rig.mounted(1));
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.mounted(2));
    TEST_ASSERT_EQUAL_STRING("", rig.mounted(3));
    TEST_ASSERT_EQUAL_STRING("demo-dos.atr", rig.mounted(4));
    for (unsigned n = 5; n <= 8; ++n) {
        TEST_ASSERT_EQUAL_STRING("", rig.mounted(n));
    }
}

// C1, C3: D1 comes back assigned (the menu keeps D1), D2 mounted; A boots D1 afterwards.
void test_a_restored_d1_is_assigned_and_d2_is_mounted()
{
    Rig rig;
    rig.restored(memoryOf({"demo-dos.atr", "demo-uno.atr"}));
    const MenuReply main = rig.press(kNoKey);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, main.action());
    assert_line(main, driveLine(1), "D1 DEMO-DOS.ATR");
    assert_line(main, driveLine(2), "D2 DEMO-UNO.ATR");
    assert_line(main, rb::host::kMainMessage, " UNIDADES RESTAURADAS");
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());
    TEST_ASSERT_EQUAL_STRING(rb::host::kMenuLocation, rig.mounted(1));
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.mounted(2));
    TEST_ASSERT_FALSE(rig.controller->memoryChanged());  // restoring is not a change to save
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionBoot, rig.press(KA).action());
    rig.step();
    TEST_ASSERT_EQUAL_STRING("demo-dos.atr", rig.mounted(1));
}

void test_nothing_remembered_gives_the_plain_main_screen()
{
    Rig rig;
    rig.restored();
    const MenuReply main = rig.press(kNoKey);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, main.action());
    assert_line(main, rb::host::kMainMessage, "");
    assert_line(main, driveLine(1), "D1 SIN DISCO");
    TEST_ASSERT_FALSE(rig.controller->memoryChanged());
}

// C4: an image no longer in the folder leaves its drive empty; the others come back.
void test_a_missing_image_is_reported_and_the_others_are_restored()
{
    Rig rig;
    rig.restored(memoryOf({"", "juego.atr", "demo-uno.atr"}));
    const MenuReply main = rig.press(kNoKey);
    assert_line(main, rb::host::kMainMessage, " D2: FALTA juego.atr");
    assert_line(main, driveLine(2), "D2 SIN DISCO");
    TEST_ASSERT_EQUAL_STRING("", rig.mounted(2));
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.mounted(3));
}

// A damaged image is refused, on D1 (validated) and on D3 (mounted); the first is named.
void test_an_invalid_image_is_reported_and_left_empty()
{
    Rig rig;
    rig.restored(memoryOf({"rota.atr", "demo-uno.atr", "rota.atr"}));
    const MenuReply main = rig.press(kNoKey);
    assert_line(main, rb::host::kMainMessage, " D1: INVALIDA ATR INVALIDA +1");
    assert_line(main, driveLine(1), "D1 SIN DISCO");
    assert_line(main, driveLine(3), "D3 SIN DISCO");
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.mounted(2));
    TEST_ASSERT_EQUAL_STRING("", rig.mounted(3));
    TEST_ASSERT_EQUAL_UINT(1, rig.repository->openFiles());  // D2 only
}

// C12: no card. The menu works at once and says why; A is enabled.
void test_a_failed_card_leaves_the_menu_working_with_the_reason()
{
    Rig rig;
    rig.controller->restoreFailed(" MICROSD: SIN TARJETA");
    TEST_ASSERT_FALSE(rig.controller->restoring());
    const MenuReply main = rig.press(kNoKey);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, main.action());
    assert_line(main, rb::host::kMainMessage, " MICROSD: SIN TARJETA");
    assert_line(rig.press(KA), rb::host::kMainMessage, " D1 SIN DISCO: ABRE LA FUENTE 1");
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutList, rig.keys({K1}).layout());
}

// --- Saving after a change ----------------------------------------------------

// C1, C8: an applied mount is a new version; GUARDANDO until its save is confirmed.
void test_an_applied_mount_shows_guardando_then_guardado()
{
    Rig rig;
    rig.restored();
    rig.press(K1);
    rig.press(KB);
    rig.press(K2);  // requested, not applied yet
    TEST_ASSERT_FALSE(rig.controller->memoryChanged());
    rig.step();
    DriveMemory memory;
    const uint32_t version = rig.take(memory);
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", memory.name(2));
    TEST_ASSERT_EQUAL_STRING("", memory.name(1));
    const MenuReply saving = rig.press(kPoll);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionWait, saving.action());  // the Atari polls for the result
    assert_line(saving, rb::host::kMainMessage, " D2: demo-uno.atr GUARDANDO");
    rig.controller->saveResult(version, true, 0);
    const MenuReply saved = rig.press(kPoll);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, saved.action());
    assert_line(saved, rb::host::kMainMessage, " D2: demo-uno.atr GUARDADO");
}

void test_a_failed_save_says_why_and_the_drive_keeps_working()
{
    Rig rig;
    rig.restored();
    rig.keys({K1, KB, K2});
    DriveMemory memory;
    rig.controller->saveResult(rig.take(memory), false, ENOSPC);
    const MenuReply failed = rig.press(kPoll);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, failed.action());
    assert_line(failed, rb::host::kMainMessage, " NO SE GUARDO: TARJETA LLENA");
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.mounted(2));
    TEST_ASSERT_FALSE(rig.controller->memoryChanged());
}

// Only the newest version confirms; an older result leaves GUARDANDO on screen.
void test_the_result_of_an_older_version_does_not_confirm()
{
    Rig rig;
    rig.restored();
    rig.keys({K1, KB, K2});
    DriveMemory first;
    const uint32_t v1 = rig.take(first);
    rig.keys({K1, KA, K3});
    rig.controller->saveResult(v1, true, 0);
    const MenuReply still = rig.press(kPoll);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionWait, still.action());
    assert_line(still, rb::host::kMainMessage, " D3: demo-dos.atr GUARDANDO");
    DriveMemory second;
    const uint32_t v2 = rig.take(second);
    TEST_ASSERT_NOT_EQUAL(v1, v2);
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", second.name(2));
    TEST_ASSERT_EQUAL_STRING("demo-dos.atr", second.name(3));
    rig.controller->saveResult(v2, true, 0);
    assert_line(rig.press(kPoll), rb::host::kMainMessage, " D3: demo-dos.atr GUARDADO");
}

// C5: an unmount is remembered like a mount.
void test_an_unmount_changes_the_snapshot()
{
    Rig rig;
    rig.restored(memoryOf({"", "demo-uno.atr"}));
    rig.keys({KU, K2});
    DriveMemory memory;
    rig.take(memory);
    TEST_ASSERT_TRUE(memory == DriveMemory{});
}

// A mount still in the mailbox is not remembered: if it is refused, nothing of it was saved.
void test_a_mount_in_flight_is_not_in_the_snapshot()
{
    Rig rig;
    rig.restored();
    rig.keys({K1, KA, K1});  // D1 assigned: a change
    rig.press(K1);
    rig.press(KB);
    rig.press(K2);  // D2 requested, not applied
    TEST_ASSERT_FALSE(rig.controller->memoryChanged());
    rig.step();
    DriveMemory memory;
    rig.take(memory);
    TEST_ASSERT_EQUAL_STRING("demo-dos.atr", memory.name(1));
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", memory.name(2));
}

// Emptying D1 (assigned, never mounted) is remembered too.
void test_emptying_d1_changes_the_snapshot()
{
    Rig rig;
    rig.restored(memoryOf({"demo-dos.atr"}));
    const MenuReply emptied = rig.keys({KU, K1});
    assert_line(emptied, rb::host::kMainMessage, " D1 DESMONTADA GUARDANDO");
    DriveMemory memory;
    rig.take(memory);
    TEST_ASSERT_TRUE(memory == DriveMemory{});
}

// D1 is assigned, never mounted, and its assignment is still a change to remember.
void test_assigning_d1_changes_the_snapshot_and_keeps_the_menu_on_d1()
{
    Rig rig;
    rig.restored();
    const MenuReply assigned = rig.keys({K1, KA, K1});
    assert_line(assigned, rb::host::kMainMessage, " D1: demo-dos.atr (A ARRANCA) GUARDANDO");
    DriveMemory memory;
    rig.take(memory);
    TEST_ASSERT_EQUAL_STRING("demo-dos.atr", memory.name(1));
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());
    TEST_ASSERT_EQUAL_STRING(rb::host::kMenuLocation, rig.mounted(1));
}

// Booting with A and coming back to the menu (button A, console `menu`) are not changes.
void test_boot_and_return_to_menu_do_not_change_the_snapshot()
{
    Rig rig;
    rig.restored(memoryOf({"demo-dos.atr", "demo-uno.atr"}));
    rig.press(KA);
    rig.step();
    TEST_ASSERT_EQUAL_STRING("demo-dos.atr", rig.mounted(1));
    TEST_ASSERT_FALSE(rig.controller->memoryChanged());
    rig.controller->onColdReset();
    rig.controller->service();
    rig.step();
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());
    TEST_ASSERT_FALSE(rig.controller->memoryChanged());
    assert_line(rig.press(kNoKey), driveLine(1), "D1 DEMO-DOS.ATR");
}

// Without beginRestore (retrobridge_host), nothing is remembered and nothing changes on screen.
void test_without_persistence_a_mount_says_what_it_always_said()
{
    Rig rig{false};
    const MenuReply mounted = rig.keys({K1, KB, K2});
    TEST_ASSERT_FALSE(rig.controller->memoryChanged());
    TEST_ASSERT_FALSE(rig.controller->restoring());
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, mounted.action());
    assert_line(mounted, rb::host::kMainMessage, " D2: demo-uno.atr");
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_restoring_shows_its_screen_and_ignores_every_key_even_a);
    RUN_TEST(test_a_cold_reset_while_restoring_keeps_the_restoring_screen);
    RUN_TEST(test_service_before_restore_waits_and_mounts_nothing);
    RUN_TEST(test_a_restored_d1_is_assigned_and_d2_is_mounted);
    RUN_TEST(test_nothing_remembered_gives_the_plain_main_screen);
    RUN_TEST(test_a_missing_image_is_reported_and_the_others_are_restored);
    RUN_TEST(test_an_invalid_image_is_reported_and_left_empty);
    RUN_TEST(test_a_failed_card_leaves_the_menu_working_with_the_reason);
    RUN_TEST(test_an_applied_mount_shows_guardando_then_guardado);
    RUN_TEST(test_a_failed_save_says_why_and_the_drive_keeps_working);
    RUN_TEST(test_the_result_of_an_older_version_does_not_confirm);
    RUN_TEST(test_an_unmount_changes_the_snapshot);
    RUN_TEST(test_emptying_d1_changes_the_snapshot);
    RUN_TEST(test_a_mount_in_flight_is_not_in_the_snapshot);
    RUN_TEST(test_assigning_d1_changes_the_snapshot_and_keeps_the_menu_on_d1);
    RUN_TEST(test_boot_and_return_to_menu_do_not_change_the_snapshot);
    RUN_TEST(test_without_persistence_a_mount_says_what_it_always_said);
    return UNITY_END();
}
