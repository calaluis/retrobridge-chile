// DriveMemoryLink (menu-unidades-persistentes T6; C1, C8, C12): what the firmware does with the
// drive memory between finish() and the SIO task. Saving is on only when the save worker runs and
// the first read of the slots gave Loaded or Empty; otherwise it stays off for the whole session.

#include <array>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <system_error>

#include "rb/app/drive_bay.hpp"
#include "rb/app/inline_drive_mailbox.hpp"
#include "rb/app/usecases/apply_drive_request.hpp"
#include "rb/menu/drive_memory.hpp"
#include "rb/menu/drive_memory_link.hpp"
#include "rb/menu/drive_slot_store.hpp"
#include "rb/menu/host_image_repository.hpp"
#include "rb/menu/menu_catalog.hpp"
#include "rb/menu/menu_controller.hpp"
#include "rb/menu/menu_image.hpp"
#include "rb/menu/menu_sources.hpp"
#include "rb/menu/storage_reason.hpp"
#include "unity.h"

#ifndef RB_TEST_MENU_DIR
#error "RB_TEST_MENU_DIR must hold the images of make_atr.py --demo"
#endif
#ifndef RB_TEST_SLOT_DIR
#error "RB_TEST_SLOT_DIR must name a scratch directory"
#endif

using rb::core::Span;
using rb::domain::DriveNumber;
using rb::host::DriveMemory;
using rb::host::DriveMemoryLink;
using rb::host::DriveMemoryPlan;
using rb::host::MenuReply;
using rb::host::SlotFileStore;
using Status = rb::host::SlotFileStore::LoadStatus;

namespace {

// Atari keyboard codes. The demo folder lists demo-dos.atr (A), demo-uno.atr (B), rota.atr (C).
constexpr uint8_t K1 = 0x1F, K2 = 0x1E, KB = 0x15;
constexpr uint8_t kPoll = static_cast<uint8_t>(rb::host::kMenuPollKey);
const char* const kDir = RB_TEST_SLOT_DIR;

std::string path_of(const char* name) { return std::string(kDir) + "/" + name; }

DriveNumber drive(unsigned n) { return DriveNumber::from(static_cast<uint8_t>(n)).value(); }

void assert_line(const MenuReply& reply, unsigned offset, const char* text)
{
    std::array<uint8_t, 40> expected{};
    rb::host::screenLine(text, Span<uint8_t>{expected});
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected.data(), reply.screen() + offset, 40, text);
}

DriveMemory memoryOf(std::initializer_list<const char*> names)
{
    DriveMemory memory;
    unsigned n = 1;
    for (const char* name : names) {
        TEST_ASSERT_TRUE(memory.set(n++, name));
    }
    return memory;
}

// The menu-sio wiring minus SIO, as after start(): menu on D1, then beginRestore().
struct Rig {
    rb::host::MenuCatalog catalog;
    rb::host::MenuSources sources;
    rb::host::MenuImage menu;
    std::optional<rb::host::HostImageRepository> repository;
    rb::app::DriveBay bay{8};
    rb::app::InlineDriveMailbox mailbox;
    rb::app::ApplyDriveRequest apply{bay, mailbox};
    std::optional<rb::host::MenuController> controller;

    Rig()
    {
        TEST_ASSERT_TRUE(rb::host::readCatalog(RB_TEST_MENU_DIR, catalog));
        repository.emplace(RB_TEST_MENU_DIR, menu);
        controller.emplace(catalog, sources, *repository, mailbox, nullptr);
        controller->onColdReset();
        controller->service();
        step();
        controller->beginRestore();
    }

    void step()
    {
        apply.execute();
        controller->service();
    }

    MenuReply press(uint8_t code) { return controller->key(code); }

    void keys(std::initializer_list<uint8_t> codes)
    {
        for (uint8_t code : codes) {
            press(code);
            step();
        }
    }

    const char* mounted(unsigned n) const
    {
        const auto* location = bay.mountedLocation(drive(n));
        return location != nullptr ? location->c_str() : "";
    }
};

// rb_drive_save without FreeRTOS: saves at once into `store` and keeps the result for poll().
struct InlineSaver final : DriveMemoryLink::Saver {
    explicit InlineSaver(SlotFileStore& s) : store(s) {}
    void submit(uint32_t version, const DriveMemory& memory) noexcept override
    {
        const SlotFileStore::SaveResult saved = store.save(memory);
        pending = true;
        lastVersion = version;
        lastOk = saved.ok;
        lastError = saved.error;
    }
    bool poll(uint32_t& version, bool& ok, int& error) noexcept override
    {
        if (!pending) {
            return false;
        }
        pending = false;
        version = lastVersion;
        ok = lastOk;
        error = lastError;
        return true;
    }
    SlotFileStore& store;
    bool pending = false;
    uint32_t lastVersion = 0;
    bool lastOk = false;
    int lastError = 0;
};

// Runs the SIO task's part until the restore ends (D2..D8 mount one per step).
void serviceUntilRestored(Rig& rig, DriveMemoryLink& link, const DriveMemoryPlan& plan, InlineSaver& saver)
{
    for (int i = 0; i < 20; ++i) {
        (void)link.apply(*rig.controller, plan, true);
        link.saves(*rig.controller, plan, saver);
        rig.step();
        if (link.applied() && !rig.controller->restoring()) {
            return;
        }
    }
    TEST_FAIL_MESSAGE("the restore did not end");
}

// The card cannot read the slot for now: the file is set aside and a directory stands in for the
// read error. makeReadableAgain() puts the same file back, as a card that reads again.
void makeUnreadable(const char* slot)
{
    std::error_code error;
    std::filesystem::rename(path_of(slot), path_of(slot) + ".aside", error);
    TEST_ASSERT_FALSE(error);
    TEST_ASSERT_TRUE(std::filesystem::create_directory(path_of(slot), error));
}

void makeReadableAgain(const char* slot)
{
    std::error_code error;
    TEST_ASSERT_TRUE(std::filesystem::remove(path_of(slot), error));
    std::filesystem::rename(path_of(slot) + ".aside", path_of(slot), error);
    TEST_ASSERT_FALSE(error);
}

}  // namespace

void setUp()
{
    std::error_code error;
    std::filesystem::remove_all(kDir, error);
    std::filesystem::create_directories(kDir, error);
}

void tearDown() {}

// C8, C12 regression: the first read fails with E0 on the card, the card then reads again, and a
// drive changes. The change works in the session, the menu says NO SE GUARDO, and the next
// power-up still restores E0.
void test_a_failed_first_read_keeps_saving_off_and_e0_survives()
{
    {  // E0: D2 = demo-dos.atr, confirmed in slot A in an earlier session
        SlotFileStore earlier{kDir};
        (void)earlier.load();
        TEST_ASSERT_TRUE(earlier.save(memoryOf({"", "demo-dos.atr"})).ok);
    }
    // Slot B was created empty (invalid) on first use; E0 is in A, and A cannot be read now.
    makeUnreadable(SlotFileStore::kSlotA);

    Rig rig;
    SlotFileStore store{kDir};
    const SlotFileStore::LoadResult first = store.load();
    TEST_ASSERT_EQUAL(Status::Error, first.status);
    DriveMemoryPlan plan;
    rb::host::planDriveMemory(nullptr, 0, &first, true, plan);
    TEST_ASSERT_FALSE(plan.saveAllowed);
    InlineSaver saver{store};
    DriveMemoryLink link;
    serviceUntilRestored(rig, link, plan, saver);

    std::string memoria = std::string(" MEMORIA: ") + rb::host::storageReason(first.error);
    assert_line(rig.press(kPoll), rb::host::kMainMessage, memoria.c_str());  // the menu works, says why

    makeReadableAgain(SlotFileStore::kSlotA);  // the card reads again: saving stays off all the same
    rig.keys({K1, KB, K2});                   // D2 = demo-uno.atr
    link.saves(*rig.controller, plan, saver);
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.mounted(2));  // the change works in this session
    TEST_ASSERT_EQUAL_UINT32(0, link.submitted());
    TEST_ASSERT_FALSE(saver.pending);
    std::string refused = std::string(" NO SE GUARDO: ") + rb::host::storageReason(plan.refusal);
    const MenuReply shown = rig.press(kPoll);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, shown.action());
    assert_line(shown, rb::host::kMainMessage, refused.c_str());

    SlotFileStore nextBoot{kDir};
    const SlotFileStore::LoadResult again = nextBoot.load();
    TEST_ASSERT_EQUAL(Status::Loaded, again.status);
    TEST_ASSERT_TRUE(again.memory == memoryOf({"", "demo-dos.atr"}));  // E0
}

// The normal path: restored from the card, a change goes to the worker and comes back GUARDADO.
void test_a_good_first_read_restores_and_saves_through_the_worker()
{
    {
        SlotFileStore earlier{kDir};
        (void)earlier.load();
        TEST_ASSERT_TRUE(earlier.save(memoryOf({"", "demo-dos.atr"})).ok);
    }
    Rig rig;
    SlotFileStore store{kDir};
    const SlotFileStore::LoadResult first = store.load();
    TEST_ASSERT_EQUAL(Status::Loaded, first.status);
    DriveMemoryPlan plan;
    rb::host::planDriveMemory(nullptr, 0, &first, true, plan);
    TEST_ASSERT_TRUE(plan.saveAllowed);
    InlineSaver saver{store};
    DriveMemoryLink link;
    serviceUntilRestored(rig, link, plan, saver);
    TEST_ASSERT_EQUAL_STRING("demo-dos.atr", rig.mounted(2));

    rig.keys({K1, KB, K2});
    link.saves(*rig.controller, plan, saver);
    TEST_ASSERT_EQUAL_UINT32(1, link.submitted());
    link.saves(*rig.controller, plan, saver);  // the result comes back
    assert_line(rig.press(kPoll), rb::host::kMainMessage, " D2: demo-uno.atr GUARDADO");

    SlotFileStore nextBoot{kDir};
    TEST_ASSERT_TRUE(nextBoot.load().memory == memoryOf({"", "demo-uno.atr"}));
}

void test_nothing_remembered_restores_empty_and_saves()
{
    SlotFileStore store{kDir};
    const SlotFileStore::LoadResult first = store.load();
    TEST_ASSERT_EQUAL(Status::Empty, first.status);
    DriveMemoryPlan plan;
    rb::host::planDriveMemory(nullptr, 0, &first, true, plan);
    TEST_ASSERT_TRUE(plan.restore);
    TEST_ASSERT_TRUE(plan.saveAllowed);
}

void test_a_card_failure_says_microsd_and_keeps_saving_off()
{
    DriveMemoryPlan plan;
    rb::host::planDriveMemory("NO MONTADA", 0, nullptr, true, plan);
    TEST_ASSERT_FALSE(plan.restore);
    TEST_ASSERT_FALSE(plan.saveAllowed);
    TEST_ASSERT_EQUAL_STRING(" MICROSD: NO MONTADA", plan.message);
    TEST_ASSERT_NOT_EQUAL(0, plan.refusal);

    Rig rig;
    SlotFileStore store{kDir};
    InlineSaver saver{store};
    DriveMemoryLink link;
    serviceUntilRestored(rig, link, plan, saver);
    assert_line(rig.press(kPoll), rb::host::kMainMessage, " MICROSD: NO MONTADA");
    rig.keys({K1, KB, K2});
    link.saves(*rig.controller, plan, saver);
    TEST_ASSERT_EQUAL_UINT32(0, link.submitted());
    TEST_ASSERT_FALSE(std::filesystem::exists(path_of(SlotFileStore::kSlotA)));  // nothing written
}

void test_without_a_worker_a_change_is_refused_at_once()
{
    SlotFileStore store{kDir};
    const SlotFileStore::LoadResult first = store.load();
    DriveMemoryPlan plan;
    rb::host::planDriveMemory(nullptr, 0, &first, false, plan);
    TEST_ASSERT_TRUE(plan.restore);
    TEST_ASSERT_FALSE(plan.saveAllowed);

    Rig rig;
    InlineSaver saver{store};
    DriveMemoryLink link;
    serviceUntilRestored(rig, link, plan, saver);
    rig.keys({K1, KB, K2});
    link.saves(*rig.controller, plan, saver);
    TEST_ASSERT_EQUAL_UINT32(0, link.submitted());
    std::string refused = std::string(" NO SE GUARDO: ") + rb::host::storageReason(ENOMEM);
    assert_line(rig.press(kPoll), rb::host::kMainMessage, refused.c_str());  // never GUARDANDO forever
}

// Names are looked up in the catalog: a restore waits for it; a failure does not.
void test_a_restore_waits_for_the_catalog()
{
    DriveMemoryPlan plan;
    const SlotFileStore::LoadResult empty{};
    rb::host::planDriveMemory(nullptr, 0, &empty, true, plan);
    Rig rig;
    DriveMemoryLink link;
    TEST_ASSERT_FALSE(link.apply(*rig.controller, plan, false));
    TEST_ASSERT_TRUE(rig.controller->restoring());
    TEST_ASSERT_TRUE(link.apply(*rig.controller, plan, true));
    TEST_ASSERT_FALSE(link.apply(*rig.controller, plan, true));  // once
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_a_failed_first_read_keeps_saving_off_and_e0_survives);
    RUN_TEST(test_a_good_first_read_restores_and_saves_through_the_worker);
    RUN_TEST(test_nothing_remembered_restores_empty_and_saves);
    RUN_TEST(test_a_card_failure_says_microsd_and_keeps_saving_off);
    RUN_TEST(test_without_a_worker_a_change_is_refused_at_once);
    RUN_TEST(test_a_restore_waits_for_the_catalog);
    return UNITY_END();
}
