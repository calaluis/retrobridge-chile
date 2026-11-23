// Network restore of remembered TNFS drives (menu-tnfs-persistente T6; AC1 to AC5 and AC9 on
// the host): after the local drives, each drive remembered from a TNFS server shows
// "Dn: CARGANDO <name>" and comes back in the background, one at a time, from the server it was
// remembered with. Deadlines (30 s for the Wi-Fi, the first answer and between two data) run on a
// clock the test moves by hand; the loader runs on its own thread over the fake server, so the
// test steps the controller until the result is there. A GatedPort holds READs to keep a
// restore in progress. The rig is test_menu_tnfs's, with its own local folder.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include "menu_tnfs_rig.hpp"

namespace {

using rb::host::DriveMemory;
using Origin = rb::host::DriveMemory::Origin;

// The controller's clock: moved by the test only (the loader keeps fakeClock).
struct HandClock final : rb::app::Clock {
    uint64_t nowMicros() const override { return now; }
    void waitUntil(uint64_t micros) override { now = micros > now ? micros : now; }
    uint64_t now = 0;
};

struct Gate final : rb::host::NetworkGate {
    rb::host::NetworkState state() const noexcept override { return now; }
    rb::host::NetworkState now = rb::host::NetworkState::Ready;
};

Bytes goodXex()  // a program at $3000 (an RTS) with its RUNAD: two segments
{
    return {0xFF, 0xFF, 0x00, 0x30, 0x00, 0x30, 0x60, 0xE0, 0x02, 0xE1, 0x02, 0x00, 0x30};
}

void writeFolder()  // the local folder: A DISCO.ATR
{
    const std::string dir = RB_TEST_MENU_DIR;
    std::error_code ignored;
    std::filesystem::create_directories(dir, ignored);
    std::ofstream out(dir + "/DISCO.ATR", std::ios::binary | std::ios::trunc);
    const Bytes disk = atr(720, 1);
    out.write(reinterpret_cast<const char*>(disk.data()), static_cast<std::streamsize>(disk.size()));
}

constexpr const char* kLogoName = "ATARI LOGO.ATR";

// DriveMemory is ~3.5 KB: on the heap (plan R3).
std::unique_ptr<DriveMemory> logoOn(unsigned drive, const char* path = kLogoPath, const char* name = kLogoName)
{
    auto memory = std::make_unique<DriveMemory>();
    TEST_ASSERT_TRUE(memory->setNet(drive, Origin::NetAtr, "pulgarcito", path, name));
    return memory;
}

// Power-up: the menu on D1, then the drive memory; the local part ends before this returns.
void powerUp(Rig& rig, HandClock& clock, const DriveMemory& memory, const Gate* gate = nullptr)
{
    rig.start();
    rig.controller->setClock(&clock);
    if (gate != nullptr) {
        rig.controller->setNetworkGate(gate);
    }
    rig.controller->beginRestore();
    rig.controller->restore(memory);
    for (int i = 0; i < 20 && rig.controller->restoring(); ++i) {
        rig.step();
    }
    TEST_ASSERT_FALSE(rig.controller->restoring());
}

// Steps the controller (real time, the loader thread works meanwhile) until `done`.
template <class Done>
void until(Rig& rig, Done done)
{
    for (int i = 0; i < 3000 && !done(); ++i) {
        rig.step();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    TEST_ASSERT_TRUE_MESSAGE(done(), "the restore did not end");
}

std::unique_ptr<DriveMemory> snapshotOf(Rig& rig)
{
    auto out = std::make_unique<DriveMemory>();
    (void)rig.controller->snapshot(*out);
    return out;
}

void assertRemembered(Rig& rig, unsigned drive, const char* path = kLogoPath, const char* name = kLogoName)
{
    const auto memory = snapshotOf(rig);
    TEST_ASSERT_TRUE(memory->origin(drive) == Origin::NetAtr);
    TEST_ASSERT_EQUAL_STRING("pulgarcito", memory->server(drive));
    TEST_ASSERT_EQUAL_STRING(path, memory->path(drive));
    TEST_ASSERT_EQUAL_STRING(name, memory->name(drive));
}

// The controller's repository, failing on purpose when it opens a TNFS slot (a restored image).
struct FaultyRepository final : rb::app::ImageRepository {
    explicit FaultyRepository(rb::app::ImageRepository& inner) : inner_(inner) {}
    std::optional<ErrorCode> failTnfs;

protected:
    rb::core::Result<rb::domain::ImageByteSource*> doOpen(const rb::app::ImageLocation& location) override
    {
        if (failTnfs && std::strncmp(location.c_str(), "*tnfs*", 6) == 0) {
            return rb::core::Failure{*failTnfs};
        }
        return inner_.open(location);
    }
    void doClose(rb::domain::ImageByteSource& source) override { inner_.close(&source); }

private:
    rb::app::ImageRepository& inner_;
};

bool failedWith(Rig& rig, const char* text) { return rig.events.has(text); }

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// AC1: D2 shows CARGANDO, then its name once the image is mounted; nothing new to save.
void test_a_remembered_atr_shows_cargando_then_mounts_on_d2()
{
    Rig rig;
    HandClock clock;
    rig.gated.gate(tnfs::cmd::kRead);
    powerUp(rig, clock, *logoOn(2));
    rig.step();  // the restore asks the server
    const MenuReply loading = rig.press(kRedraw);
    assert_line(loading, driveLine(2), "D2: CARGANDO ATARI LOGO.ATR");
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionWait, loading.action());  // the Atari polls for the result
    rig.gated.open();
    until(rig, [&] { return rig.controller->driveName(2) != nullptr; });
    rig.step();  // the mount is applied
    TEST_ASSERT_EQUAL_STRING_LEN("*tnfs*", rig.mounted(2), 6);
    assert_line(rig.press(kRedraw), driveLine(2), "D2 ATARI LOGO.ATR                SL");
    TEST_ASSERT_EQUAL_STRING("pulgarcito", rig.restore.renamedTo);
    TEST_ASSERT_FALSE(rig.controller->memoryChanged());  // what was remembered is what is mounted
    assertRemembered(rig, 2);
}

// AC2: A with D1 still on its way says to wait; once the .xex is there, A boots it.
void test_a_remembered_xex_on_d1_waits_then_a_boots_it()
{
    Rig rig;
    HandClock clock;
    rig.server.addFile("/Juegos/nave.xex", goodXex());
    auto memory = std::make_unique<DriveMemory>();
    TEST_ASSERT_TRUE(memory->setNet(1, Origin::NetXex, "pulgarcito", "/Juegos/nave.xex", "NAVE.XEX"));
    rig.gated.gate(tnfs::cmd::kRead);
    powerUp(rig, clock, *memory);
    rig.step();
    const MenuReply early = rig.press(KA);
    TEST_ASSERT_NOT_EQUAL(rb::host::kMenuActionBoot, early.action());
    assert_line(early, kMainMessage, " D1 CARGANDO, ESPERA");
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());
    rig.gated.open();
    until(rig, [&] { return rig.controller->driveName(1) != nullptr; });
    TEST_ASSERT_EQUAL_STRING("NAVE.XEX", rig.controller->driveName(1));
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionBoot, rig.press(KA).action());
    rig.step();
    TEST_ASSERT_EQUAL_STRING_LEN("*tnfs*", rig.mounted(1), 6);
    const auto saved = snapshotOf(rig);
    TEST_ASSERT_TRUE(saved->origin(1) == Origin::NetXex);
}

// AC3: D1 from the microSD is ready at once and A boots it while D2 still loads.
void test_a_local_d1_boots_while_d2_is_still_loading()
{
    Rig rig;
    HandClock clock;
    auto memory = logoOn(2);
    TEST_ASSERT_TRUE(memory->set(1, "DISCO.ATR"));
    rig.gated.gate(tnfs::cmd::kRead);
    powerUp(rig, clock, *memory);
    rig.step();
    rig.gated.awaitHeld();  // D2's download is under way
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionBoot, rig.press(KA).action());
    rig.step();
    TEST_ASSERT_EQUAL_STRING("DISCO.ATR", rig.mounted(1));
    rig.gated.open();
    until(rig, [&] { return rig.controller->driveName(2) != nullptr; });
}

// AC4: no Wi-Fi in the 30 s after power-up: SIN RED, nothing asked, still remembered.
void test_without_wifi_for_30_s_d2_says_sin_red_and_stays_remembered()
{
    Rig rig;
    HandClock clock;
    Gate gate;
    gate.now = rb::host::NetworkState::Connecting;
    powerUp(rig, clock, *logoOn(2), &gate);
    clock.now = 29000000;
    rig.step();
    assert_line(rig.press(kRedraw), driveLine(2), "D2: CARGANDO ATARI LOGO.ATR");
    clock.now = 30001000;
    rig.step();
    const MenuReply after = rig.press(kRedraw);
    assert_line(after, driveLine(2), "D2: SIN RED");
    assert_line(after, kMainMessage, " D2: SIN RED");
    TEST_ASSERT_EQUAL_size_t(0, rig.server.log().size());
    TEST_ASSERT_FALSE(rig.controller->memoryChanged());
    assertRemembered(rig, 2);
    gate.now = rb::host::NetworkState::Ready;  // too late: this session does not ask any more
    rig.step();
    TEST_ASSERT_EQUAL_size_t(0, rig.server.log().size());
}

// AC4: the server answers nothing for 30 s once asked: SIN RED, the job is cancelled.
void test_a_silent_server_for_30_s_says_sin_red_and_stays_remembered()
{
    Rig rig;
    HandClock clock;
    rig.gated.gate(tnfs::cmd::kRead);
    powerUp(rig, clock, *logoOn(2));
    rig.step();
    rig.gated.awaitHeld();
    clock.now = 29000000;
    rig.step();
    assert_line(rig.press(kRedraw), driveLine(2), "D2: CARGANDO ATARI LOGO.ATR");
    clock.now = 30001000;
    rig.step();
    assert_line(rig.press(kRedraw), driveLine(2), "D2: SIN RED");
    TEST_ASSERT_TRUE(failedWith(rig, "D2 net failed: no network (deadline)"));
    assertRemembered(rig, 2);
    rig.gated.open();  // the cancelled job ends; its late image changes nothing
    for (int i = 0; i < 200; ++i) {
        rig.step();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    TEST_ASSERT_EQUAL_STRING("", rig.mounted(2));
    TEST_ASSERT_NULL(rig.controller->driveName(2));
}

// AC4, plan §5 (integrated): one block every 20 s for 2 min keeps the restore alive, though the
// integer percent hardly moves (180 READs: about half a point a block); only the bytes count.
// Then 29 s without data: still loading; past 30 s without data: SIN RED, still remembered.
void test_slow_progress_never_expires_but_30_s_without_data_does()
{
    Rig rig;
    HandClock clock;
    rig.gated.gate(tnfs::cmd::kRead);
    powerUp(rig, clock, *logoOn(2));
    rig.step();             // the restore asks the server at 0 s
    rig.gated.awaitHeld();  // the first READ is held
    for (int block = 1; block <= 6; ++block) {
        clock.now += 20000000;   // 20 s later, one block arrives
        rig.gated.permit(1);
        rig.gated.awaitHeld();   // its data is in: the next READ is held
        rig.step();              // the controller sees the bytes grow at this time
        assert_line(rig.press(kRedraw), driveLine(2), "D2: CARGANDO ATARI LOGO.ATR");
    }
    TEST_ASSERT_TRUE(clock.now >= 120000000u);  // 2 min: far past any single 30 s deadline
    clock.now += 29000000;
    rig.step();
    assert_line(rig.press(kRedraw), driveLine(2), "D2: CARGANDO ATARI LOGO.ATR");
    clock.now += 2000000;  // 31 s since the last data
    rig.step();
    assert_line(rig.press(kRedraw), driveLine(2), "D2: SIN RED");
    TEST_ASSERT_TRUE(failedWith(rig, "D2 net failed: no network (deadline)"));
    assertRemembered(rig, 2);
    rig.gated.open();
    for (int i = 0; i < 200; ++i) {
        rig.step();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    TEST_ASSERT_EQUAL_STRING("", rig.mounted(2));  // the cancelled download changes nothing
}

// AC4: the remembered name does not resolve: SERVIDOR NO ENCONTRADO, still remembered; and the
// next power-up, with the server back, restores it.
void test_an_unresolved_server_says_so_and_the_next_power_up_restores_it()
{
    auto saved = std::make_unique<DriveMemory>();
    {
        Rig rig;
        HandClock clock;
        rig.restore.failWith = ErrorCode::NotFound;
        powerUp(rig, clock, *logoOn(2));
        until(rig, [&] { return failedWith(rig, "D2 net failed: server not found"); });
        const MenuReply after = rig.press(kRedraw);
        assert_line(after, driveLine(2), "D2: SERVIDOR NO ENCONTRADO");
        assert_line(after, kMainMessage, " D2: SERVIDOR NO ENCONTRADO");
        (void)rig.controller->snapshot(*saved);
    }
    TEST_ASSERT_TRUE(saved->origin(2) == Origin::NetAtr);
    Rig rig;
    HandClock clock;
    powerUp(rig, clock, *saved);
    until(rig, [&] { return rig.controller->driveName(2) != nullptr; });
    rig.step();
    TEST_ASSERT_EQUAL_STRING_LEN("*tnfs*", rig.mounted(2), 6);
}

// AC5: the server says the image is not there (ENOENT): FALTA, and a new version without it.
void test_a_missing_image_says_falta_and_is_forgotten()
{
    Rig rig;
    HandClock clock;
    powerUp(rig, clock, *logoOn(2, "/nada.atr", "NADA.ATR"));
    until(rig, [&] { return failedWith(rig, "D2 net failed: missing"); });
    const MenuReply after = rig.press(kRedraw);
    assert_line(after, driveLine(2), "D2 SIN DISCO");
    assert_line(after, kMainMessage, " D2: FALTA NADA.ATR GUARDANDO");  // the reason, then the save
    TEST_ASSERT_TRUE(rig.controller->memoryChanged());
    TEST_ASSERT_TRUE(snapshotOf(rig)->origin(2) == Origin::Empty);
}

// AC5: what arrives is no disk: INVALIDA, and forgotten.
void test_an_invalid_image_says_invalida_and_is_forgotten()
{
    Rig rig;
    HandClock clock;
    powerUp(rig, clock, *logoOn(3, "/Juegos/rota.atr", "ROTA.ATR"));
    until(rig, [&] { return failedWith(rig, "D3 net failed: invalid"); });
    rig.step();
    assert_line(rig.press(kRedraw), driveLine(3), "D3 SIN DISCO");
    TEST_ASSERT_TRUE(rig.controller->memoryChanged());
    TEST_ASSERT_TRUE(snapshotOf(rig)->origin(3) == Origin::Empty);
    TEST_ASSERT_EQUAL_STRING("", rig.mounted(3));
}

// Review of PR #94: an error while applying a restored image is classified, not taken as
// INVALIDA. No free slot is NO CABE and an ambiguous error (I/O) NO SE PUDO ABRIR; both keep the
// drive's server, path and name in snapshot(), with no new version. D2 (mounted) and D1 (assigned).
void test_no_free_slot_or_an_ambiguous_error_applying_a_restored_image_keeps_the_drive()
{
    struct Case {
        unsigned drive;
        ErrorCode error;
        const char* logged;
        const char* line;
    };
    const Case cases[] = {
        {2, ErrorCode::NoFreeSlot, "D2 net failed: no room", "D2: NO CABE"},
        {2, ErrorCode::IoError, "D2 net failed: could not open", "D2: NO SE PUDO ABRIR"},
        {1, ErrorCode::NoFreeSlot, "D1 net failed: no room", "D1: NO CABE"},
        {1, ErrorCode::IoError, "D1 net failed: could not open", "D1: NO SE PUDO ABRIR"},
    };
    for (const Case& c : cases) {
        Rig rig;
        HandClock clock;
        FaultyRepository faulty(*rig.repository);
        faulty.failTnfs = c.error;
        rig.controllerRepository = &faulty;
        powerUp(rig, clock, *logoOn(c.drive));
        until(rig, [&] { return failedWith(rig, c.logged); });
        rig.step();
        const MenuReply after = rig.press(kRedraw);
        assert_line(after, driveLine(c.drive), c.line);
        TEST_ASSERT_FALSE(rig.events.has("net failed: invalid"));
        TEST_ASSERT_FALSE(rig.controller->memoryChanged());
        assertRemembered(rig, c.drive);
        awaitSlots(rig.store(), 0);  // the image's slot went back to the loader
    }
}

// Plan §3: an ambiguous refusal and an image that does not fit keep the drive remembered.
void test_an_ambiguous_refusal_and_no_room_keep_the_drive()
{
    {
        Rig rig;
        HandClock clock;
        rig.server.reject(tnfs::cmd::kStat, 0x0D);  // EACCES: not ENOENT
        powerUp(rig, clock, *logoOn(2));
        until(rig, [&] { return failedWith(rig, "D2 net failed: could not open"); });
        assert_line(rig.press(kRedraw), driveLine(2), "D2: NO SE PUDO ABRIR");
        TEST_ASSERT_FALSE(rig.controller->memoryChanged());
        assertRemembered(rig, 2);
    }
    Rig rig;
    HandClock clock;
    rig.memory.fail = true;
    powerUp(rig, clock, *logoOn(2));
    until(rig, [&] { return failedWith(rig, "D2 net failed: no room"); });
    assert_line(rig.press(kRedraw), driveLine(2), "D2: NO CABE");
    TEST_ASSERT_FALSE(rig.controller->memoryChanged());
    assertRemembered(rig, 2);
}

// AC9: source 2 now names another server; the restore asks the remembered one.
void test_the_restore_asks_the_remembered_server_whatever_source_2_says()
{
    Rig rig;
    HandClock clock;
    TEST_ASSERT_EQUAL_UINT(2, rig.sources.set(2, "srv-b"));
    powerUp(rig, clock, *logoOn(2));
    until(rig, [&] { return rig.controller->driveName(2) != nullptr; });
    TEST_ASSERT_EQUAL_STRING("pulgarcito", rig.restore.renamedTo);
    TEST_ASSERT_EQUAL_INT(0, rig.link.renames.load());  // source 2's session was not used
}

int main(void)
{
    writeFolder();
    UNITY_BEGIN();
    RUN_TEST(test_a_remembered_atr_shows_cargando_then_mounts_on_d2);
    RUN_TEST(test_a_remembered_xex_on_d1_waits_then_a_boots_it);
    RUN_TEST(test_a_local_d1_boots_while_d2_is_still_loading);
    RUN_TEST(test_without_wifi_for_30_s_d2_says_sin_red_and_stays_remembered);
    RUN_TEST(test_a_silent_server_for_30_s_says_sin_red_and_stays_remembered);
    RUN_TEST(test_slow_progress_never_expires_but_30_s_without_data_does);
    RUN_TEST(test_an_unresolved_server_says_so_and_the_next_power_up_restores_it);
    RUN_TEST(test_a_missing_image_says_falta_and_is_forgotten);
    RUN_TEST(test_an_invalid_image_says_invalida_and_is_forgotten);
    RUN_TEST(test_an_ambiguous_refusal_and_no_room_keep_the_drive);
    RUN_TEST(test_no_free_slot_or_an_ambiguous_error_applying_a_restored_image_keeps_the_drive);
    RUN_TEST(test_the_restore_asks_the_remembered_server_whatever_source_2_says);
    return UNITY_END();
}
