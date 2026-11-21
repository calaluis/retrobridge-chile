// MenuStation (menu-sio-fisico T2): the menu served as D1 through any SioTransport.
// Spec criteria 1-4 at host level: D1 boots the menu after start(), keys are
// answered, A boots the image chosen for D1, and returnToMenu() (the FujiNet
// button or the console command `menu`) gives D1 back to the menu.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

#include "fakes/fake_clock.hpp"
#include "fakes/scripted_sio_transport.hpp"
#include "rb/menu/host_image_repository.hpp"
#include "rb/menu/menu_image.hpp"
#include "rb/menu/menu_station.hpp"
#include "rb/sio/sio_checksum.hpp"
#include "unity.h"

#ifndef RB_TEST_MENU_DIR
#error "RB_TEST_MENU_DIR must hold the images of make_atr.py --demo"
#endif

using rb::core::Span;
using rb::sio::StepOutcome;

namespace {

constexpr uint8_t K1 = 0x1F, KA = 0x3F;

std::array<uint8_t, 5> frame(uint8_t device, uint8_t command, uint8_t aux1, uint8_t aux2)
{
    std::array<uint8_t, 5> bytes{device, command, aux1, aux2, 0};
    bytes[4] = rb::sio::sio_checksum(Span<const uint8_t>{bytes.data(), 4});
    return bytes;
}

struct StationRig {
    rb::host::MenuCatalog catalog;
    rb::host::MenuSources sources;
    rb::host::MenuImage menu;
    std::optional<rb::host::HostImageRepository> repository;
    rb::test::FakeClock clock;
    rb::test::ScriptedSioTransport inner{clock};
    std::optional<rb::host::MenuStation> station;
    uint8_t sequence = 0;

    StationRig()
    {
        TEST_ASSERT_TRUE(rb::host::readCatalog(RB_TEST_MENU_DIR, catalog));
        repository.emplace(RB_TEST_MENU_DIR, menu);
        station.emplace(inner, clock, rb::host::MenuStationParts{catalog, sources, *repository});
        station->start();
    }

    // Sector `n` of D1 as the Atari's OS reads it: ACK, then COMPLETE + 128 bytes + checksum.
    std::array<uint8_t, 128> readD1(uint16_t n)
    {
        inner.push_frame(frame(0x31, 0x52, static_cast<uint8_t>(n & 0xFF), static_cast<uint8_t>(n >> 8)));
        StepOutcome outcome = StepOutcome::Idle;
        for (int i = 0; i < 4 && outcome != StepOutcome::Completed; ++i) {
            outcome = station->step();
        }
        TEST_ASSERT_EQUAL(StepOutcome::Completed, outcome);
        const auto& data = inner.sent(inner.sends() - 1);
        TEST_ASSERT_EQUAL_UINT(1 + 128 + 1, data.length);
        TEST_ASSERT_EQUAL_UINT8(0x43, data.bytes[0]);
        std::array<uint8_t, 128> out{};
        std::memcpy(out.data(), data.bytes.data() + 1, out.size());
        return out;
    }

    void key(uint8_t code)
    {
        sequence = static_cast<uint8_t>((sequence + 1) & 0x07);
        inner.push_frame(frame(rb::host::kMenuDevice, rb::host::kMenuCommand, code,
                               static_cast<uint8_t>((sequence << 5) | rb::host::kMenuNoRow)));
        station->step();  // the decorator answers it: the session sees no frame
        station->step();  // a request the key made is applied
    }
};

std::array<uint8_t, 128> menuSector(uint16_t n)
{
    rb::host::MenuImage menu;
    std::array<uint8_t, 128> out{};
    TEST_ASSERT_TRUE(menu.readAt(rb::host::kMenuHeaderSize + (n - 1u) * 128u, Span<uint8_t>{out}).ok());
    return out;
}

std::array<uint8_t, 128> fileSector(const char* name, uint16_t n)
{
    const std::string path = std::string(RB_TEST_MENU_DIR) + "/" + name;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    TEST_ASSERT_NOT_NULL(file);
    std::array<uint8_t, 128> out{};
    TEST_ASSERT_EQUAL_INT(0, std::fseek(file, static_cast<long>(16 + (n - 1u) * 128u), SEEK_SET));
    TEST_ASSERT_EQUAL_UINT(out.size(), std::fread(out.data(), 1, out.size(), file));
    std::fclose(file);
    return out;
}

void bootDemoDos(StationRig& rig)
{
    rig.key(K1);  // source 1: the local list
    rig.key(KA);  // image A: DEMO-DOS.ATR
    rig.key(K1);  // for D1
    rig.key(KA);  // A boots it
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_after_start_d1_serves_the_menu_program()
{
    StationRig rig;
    TEST_ASSERT_TRUE(rig.station->menuOnD1());
    for (uint16_t n : {1, 2, 17}) {
        const auto expected = menuSector(n);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), rig.readD1(n).data(), 128);
    }
    TEST_ASSERT_TRUE(rig.inner.releases() >= 3);  // DATA IN released after every transaction
}

void test_a_boots_the_image_chosen_for_d1()
{
    StationRig rig;
    bootDemoDos(rig);
    TEST_ASSERT_FALSE(rig.station->menuOnD1());
    TEST_ASSERT_EQUAL_UINT(4, rig.station->stats().menuCommands);
    const auto expected = fileSector("demo-dos.atr", 1);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), rig.readD1(1).data(), 128);
}

void test_a_power_cycle_after_a_boots_the_same_image_again()
{
    StationRig rig;
    bootDemoDos(rig);
    const auto first = rig.readD1(1);
    const auto again = rig.readD1(1);  // the Atari is switched off and on: no reset reaches the FujiNet
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first.data(), again.data(), 128);
    TEST_ASSERT_FALSE(rig.station->menuOnD1());
}

void test_return_to_menu_gives_d1_back_to_the_menu_before_the_next_boot()
{
    StationRig rig;
    bootDemoDos(rig);
    (void)rig.readD1(1);
    rig.station->returnToMenu();  // FujiNet button A or console `menu`
    TEST_ASSERT_TRUE(rig.station->menuOnD1());
    // A D1 frame in the middle is dropped, not served from DEMO-DOS.ATR.
    const std::size_t sends = rig.inner.sends();
    rig.inner.push_frame(frame(0x31, 0x52, 1, 0));
    TEST_ASSERT_EQUAL(StepOutcome::Idle, rig.station->step());
    TEST_ASSERT_EQUAL_UINT(sends, rig.inner.sends());
    rig.station->step();  // the Atari is off for a moment: the menu is mounted
    rig.station->step();
    const auto expected = menuSector(1);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), rig.readD1(1).data(), 128);
    TEST_ASSERT_TRUE(rig.station->controller().menuOnD1());
}

void test_writes_are_refused_by_default()
{
    StationRig rig;
    bootDemoDos(rig);
    // 'W' sector 1 of D1: allowWrites is false by default, so the dispatcher NAKs it.
    rig.inner.push_frame(frame(0x31, 0x57, 1, 0));
    StepOutcome outcome = StepOutcome::Idle;
    for (int i = 0; i < 3 && outcome == StepOutcome::Idle; ++i) {
        outcome = rig.station->step();
    }
    TEST_ASSERT_EQUAL(StepOutcome::Nacked, outcome);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_after_start_d1_serves_the_menu_program);
    RUN_TEST(test_a_boots_the_image_chosen_for_d1);
    RUN_TEST(test_a_power_cycle_after_a_boots_the_same_image_again);
    RUN_TEST(test_return_to_menu_gives_d1_back_to_the_menu_before_the_next_boot);
    RUN_TEST(test_writes_are_refused_by_default);
    return UNITY_END();
}
