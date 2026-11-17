// Boot menu of retrobridge_host (Issue #38, ADR-017): catalog, in-memory menu
// disk, controller (validation on choice, return to the menu on cold reset) and
// the transport decorator that serves the menu device. The demo images are
// generated at test time by tools/make_atr.py --demo (fixture menu_images).

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

#include "fakes/fake_clock.hpp"
#include "fakes/scripted_sio_transport.hpp"
#include "host_image_repository.hpp"
#include "menu_catalog.hpp"
#include "menu_controller.hpp"
#include "menu_image.hpp"
#include "menu_sio_transport.hpp"
#include "rb/app/drive_bay.hpp"
#include "rb/app/inline_drive_mailbox.hpp"
#include "rb/app/usecases/apply_drive_request.hpp"
#include "rb/domain/disk_image.hpp"
#include "rb/sio/sio_checksum.hpp"
#include "unity.h"

#ifndef RB_TEST_MENU_DIR
#error "RB_TEST_MENU_DIR must hold the images of make_atr.py --demo"
#endif

using rb::core::ErrorCode;
using rb::core::Span;
using rb::domain::DriveNumber;
using rb::host::MenuCatalog;
using rb::host::MenuImage;
using rb::host::MenuReply;

namespace {

const DriveNumber kD1 = DriveNumber::from(1).value();

std::array<uint8_t, 40> line(const char* text)
{
    std::array<uint8_t, 40> out{};
    rb::host::screenLine(text, Span<uint8_t>{out});
    return out;
}

std::array<uint8_t, 5> frame(uint8_t device, uint8_t command, uint8_t aux1 = 0)
{
    std::array<uint8_t, 5> bytes{device, command, aux1, 0, 0};
    bytes[4] = rb::sio::sio_checksum(Span<const uint8_t>{bytes.data(), 4});
    return bytes;
}

// The --images wiring of main.cpp, minus NetSIO: the demo folder, the menu, the
// drive context and the controller. step() plays the start of SioSession::step()
// followed by main's afterStep().
struct MenuRig {
    MenuCatalog catalog;
    std::optional<MenuImage> menu;
    std::optional<rb::host::HostImageRepository> repository;
    rb::app::DriveBay bay{1};
    rb::app::InlineDriveMailbox mailbox;
    rb::app::ApplyDriveRequest apply{bay, mailbox};
    std::optional<rb::host::MenuController> controller;

    MenuRig()
    {
        TEST_ASSERT_TRUE(rb::host::readCatalog(RB_TEST_MENU_DIR, catalog));
        menu.emplace(catalog);
        repository.emplace(RB_TEST_MENU_DIR, *menu);
        controller.emplace(catalog, *repository, mailbox, nullptr);
        controller->onColdReset();
        controller->service();
        step();
    }

    void step()
    {
        apply.execute();
        controller->service();
    }

    const char* d1() const
    {
        const auto* location = bay.mountedLocation(kD1);
        return location != nullptr ? location->c_str() : "";
    }
};

void assert_reply(const MenuReply& reply, uint8_t status, const char* text)
{
    TEST_ASSERT_EQUAL_UINT8(status, reply.bytes[0]);
    const auto expected = line(text);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), reply.bytes.data() + 1, 40);
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- Catalog -----------------------------------------------------------------

void test_catalog_keeps_only_atr_names_in_case_insensitive_order()
{
    MenuCatalog catalog;
    for (const char* name : {"b.ATR", "notes.txt", "a.atr", "c.atrx", ".atr", "Z.Atr", "a.atr"}) {
        catalog.offer(name);
    }
    TEST_ASSERT_EQUAL_UINT(3, catalog.count());
    TEST_ASSERT_EQUAL_STRING("a.atr", catalog.name(0));
    TEST_ASSERT_EQUAL_STRING("b.ATR", catalog.name(1));
    TEST_ASSERT_EQUAL_STRING("Z.Atr", catalog.name(2));
}

void test_catalog_keeps_the_first_twenty_whatever_the_listing_order()
{
    MenuCatalog catalog;
    char name[16];
    for (int i = 24; i >= 0; --i) {
        std::snprintf(name, sizeof name, "n%02d.atr", i);
        catalog.offer(name);
    }
    TEST_ASSERT_EQUAL_UINT(20, catalog.count());
    TEST_ASSERT_EQUAL_UINT(25, catalog.atrSeen());
    TEST_ASSERT_EQUAL_STRING("n00.atr", catalog.name(0));
    TEST_ASSERT_EQUAL_STRING("n19.atr", catalog.name(19));
}

void test_catalog_skips_names_the_repository_could_not_open()
{
    MenuCatalog catalog;
    const std::string tooLong = std::string(124, 'x') + ".atr";  // 128 characters
    TEST_ASSERT_FALSE(catalog.offer(tooLong.c_str()));
    TEST_ASSERT_FALSE(catalog.offer("bad\x01name.atr"));
    TEST_ASSERT_FALSE(catalog.offer("a:b.atr"));
    TEST_ASSERT_FALSE(catalog.offer("a\\b.atr"));
    TEST_ASSERT_TRUE(catalog.offer((std::string(123, 'x') + ".atr").c_str()));  // 127: the limit
    TEST_ASSERT_EQUAL_UINT(1, catalog.count());
}

void test_read_catalog_lists_the_demo_folder()
{
    MenuCatalog catalog;
    TEST_ASSERT_TRUE(rb::host::readCatalog(RB_TEST_MENU_DIR, catalog));
    TEST_ASSERT_EQUAL_UINT(3, catalog.count());
    TEST_ASSERT_EQUAL_STRING("demo-dos.atr", catalog.name(0));
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", catalog.name(1));
    TEST_ASSERT_EQUAL_STRING("rota.atr", catalog.name(2));
}

void test_read_catalog_fails_on_a_missing_folder()
{
    MenuCatalog catalog;
    TEST_ASSERT_FALSE(rb::host::readCatalog(RB_TEST_MENU_DIR "/no-such-folder", catalog));
}

// --- Menu disk -----------------------------------------------------------------

void test_menu_disk_is_a_valid_single_density_atr_with_the_catalog()
{
    MenuCatalog catalog;
    catalog.offer("demo-uno.atr");
    catalog.offer("demo-dos.atr");
    MenuImage menu{catalog};
    auto image = rb::domain::DiskImage::open(menu);
    TEST_ASSERT_TRUE(image.ok());
    TEST_ASSERT_EQUAL_UINT16(720, image.value().geometry().sectorCount());

    std::array<uint8_t, 128> sector{};
    TEST_ASSERT_TRUE(image.value().readSector(rb::domain::SectorNumber::from(1).value(), Span<uint8_t>{sector}).ok());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(menu.block().data(), sector.data(), 128);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLoadSectors, sector[1]);
    TEST_ASSERT_EQUAL_UINT8(2, menu.block()[rb::host::kMenuCountOffset]);

    const auto first = line(" A  DEMO-DOS.ATR");
    const auto second = line(" B  DEMO-UNO.ATR");
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first.data(), menu.block().data() + rb::host::kMenuEntriesOffset, 40);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(second.data(), menu.block().data() + rb::host::kMenuEntriesOffset + 40, 40);
    const auto blank = line("");
    TEST_ASSERT_EQUAL_UINT8_ARRAY(blank.data(), menu.block().data() + rb::host::kMenuEntriesOffset + 80, 40);

    TEST_ASSERT_TRUE(image.value().readSector(rb::domain::SectorNumber::from(720).value(), Span<uint8_t>{sector}).ok());
    for (uint8_t byte : sector) {
        TEST_ASSERT_EQUAL_UINT8(0, byte);
    }
}

void test_menu_disk_cuts_long_names_and_masks_odd_characters()
{
    MenuCatalog catalog;
    catalog.offer("abcdefghijklmnopqrstuvwxyz0123456789~.atr");
    MenuImage menu{catalog};
    const auto expected = line(" A  ABCDEFGHIJKLMNOPQRSTUVWXYZ0123");
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), menu.block().data() + rb::host::kMenuEntriesOffset, 40);
    std::array<uint8_t, 3> codes{};
    rb::host::screenLine("a\x7F~", Span<uint8_t>{codes});
    TEST_ASSERT_EQUAL_UINT8('A' - 0x20, codes[0]);
    TEST_ASSERT_EQUAL_UINT8('?' - 0x20, codes[1]);
    TEST_ASSERT_EQUAL_UINT8('~', codes[2]);
}

void test_menu_disk_reads_are_all_or_nothing()
{
    MenuCatalog catalog;
    MenuImage menu{catalog};
    std::array<uint8_t, 4> out{};
    TEST_ASSERT_TRUE(menu.readAt(rb::host::kMenuImageSize - 4, Span<uint8_t>{out}).ok());
    const auto past = menu.readAt(rb::host::kMenuImageSize - 3, Span<uint8_t>{out});
    TEST_ASSERT_FALSE(past.ok());
    TEST_ASSERT_EQUAL(ErrorCode::OutOfBounds, past.error());
}

// --- Controller --------------------------------------------------------------

void test_start_mounts_the_menu_on_d1()
{
    MenuRig rig;
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());
    TEST_ASSERT_EQUAL_STRING(rb::host::kMenuLocation, rig.d1());
    TEST_ASSERT_FALSE(rig.controller->coldResetPending());
}

void test_valid_choice_mounts_that_image_and_closes_the_menu()
{
    MenuRig rig;
    assert_reply(rig.controller->select(1), 0, " ARRANCANDO demo-uno.atr");
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());  // applied only at the next step
    rig.step();
    TEST_ASSERT_FALSE(rig.controller->menuOnD1());
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.d1());
    TEST_ASSERT_FALSE(rig.repository->menuOpen());
    TEST_ASSERT_EQUAL_UINT(1, rig.repository->openFiles());
}

void test_invalid_image_is_refused_with_a_reason_and_nothing_is_mounted()
{
    MenuRig rig;
    assert_reply(rig.controller->select(2), 1, " ERROR: ATR INVALIDA");
    TEST_ASSERT_TRUE(rig.mailbox.idle());
    rig.step();
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());
    TEST_ASSERT_EQUAL_STRING(rb::host::kMenuLocation, rig.d1());
    TEST_ASSERT_EQUAL_UINT(0, rig.repository->openFiles());
    // Another choice still works.
    assert_reply(rig.controller->select(0), 0, " ARRANCANDO demo-dos.atr");
    rig.step();
    TEST_ASSERT_EQUAL_STRING("demo-dos.atr", rig.d1());
}

void test_index_outside_the_list_is_refused()
{
    MenuRig rig;
    assert_reply(rig.controller->select(3), 1, " ERROR: OPCION FUERA DE LA LISTA");
    TEST_ASSERT_TRUE(rig.mailbox.idle());
}

void test_cold_reset_brings_the_menu_back_and_closes_the_image()
{
    MenuRig rig;
    rig.controller->select(0);
    rig.step();
    TEST_ASSERT_EQUAL_STRING("demo-dos.atr", rig.d1());
    rig.controller->onColdReset();
    rig.controller->service();  // main's afterStep: the menu is requested
    TEST_ASSERT_FALSE(rig.controller->coldResetPending());
    rig.step();
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());
    TEST_ASSERT_EQUAL_STRING(rb::host::kMenuLocation, rig.d1());
    TEST_ASSERT_EQUAL_UINT(0, rig.repository->openFiles());
    // Second walk: the other image.
    assert_reply(rig.controller->select(1), 0, " ARRANCANDO demo-uno.atr");
    rig.step();
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.d1());
}

void test_cold_reset_with_the_menu_on_d1_changes_nothing()
{
    MenuRig rig;
    rig.controller->onColdReset();
    rig.controller->service();
    TEST_ASSERT_FALSE(rig.controller->coldResetPending());
    TEST_ASSERT_TRUE(rig.mailbox.idle());
    TEST_ASSERT_TRUE(rig.repository->menuOpen());
}

void test_choice_while_a_mount_is_in_flight_is_refused()
{
    MenuRig rig;
    rig.controller->select(0);
    assert_reply(rig.controller->select(1), 1, " ERROR: UNIDAD OCUPADA, REPITE");
}

// --- Transport decorator -------------------------------------------------------

struct DecoratorRig {
    MenuRig menu;
    rb::test::FakeClock clock;
    rb::test::ScriptedSioTransport inner{clock};
    rb::host::MenuSioTransport transport{inner, clock, *menu.controller};
};

void test_menu_command_gets_ack_then_complete_with_41_bytes_and_checksum()
{
    DecoratorRig rig;
    rig.inner.push_frame(frame(0x5A, 0x4D, 1));
    const auto received = rig.transport.waitCommandFrame(20000);
    TEST_ASSERT_FALSE(received.ok());
    TEST_ASSERT_EQUAL(ErrorCode::Timeout, received.error());

    TEST_ASSERT_EQUAL_UINT(2, rig.inner.sends());
    TEST_ASSERT_EQUAL_UINT(1, rig.inner.sent(0).length);
    TEST_ASSERT_EQUAL_UINT8(0x41, rig.inner.sent(0).bytes[0]);
    const auto& complete = rig.inner.sent(1);
    TEST_ASSERT_EQUAL_UINT(43, complete.length);
    TEST_ASSERT_EQUAL_UINT8(0x43, complete.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(0, complete.bytes[1]);
    const auto text = line(" ARRANCANDO demo-uno.atr");
    TEST_ASSERT_EQUAL_UINT8_ARRAY(text.data(), complete.bytes.data() + 2, 40);
    TEST_ASSERT_EQUAL_UINT8(rb::sio::sio_checksum(Span<const uint8_t>{complete.bytes.data() + 1, 41}),
                            complete.bytes[42]);
    TEST_ASSERT_TRUE(complete.startUs - rig.inner.sent(0).endUs >= 250);
    TEST_ASSERT_TRUE(rig.inner.releases() >= 1);
    TEST_ASSERT_EQUAL_UINT(1, rig.transport.stats().menuCommands);
}

void test_other_menu_commands_are_nacked_and_bad_checksums_ignored()
{
    DecoratorRig rig;
    rig.inner.push_frame(frame(0x5A, 0x53));
    auto bad = frame(0x5A, 0x4D, 0);
    bad[4] ^= 0xFF;
    rig.inner.push_frame(bad);
    TEST_ASSERT_FALSE(rig.transport.waitCommandFrame(20000).ok());
    TEST_ASSERT_EQUAL_UINT(1, rig.inner.sends());
    TEST_ASSERT_EQUAL_UINT8(0x4E, rig.inner.sent(0).bytes[0]);
    TEST_ASSERT_FALSE(rig.transport.waitCommandFrame(20000).ok());
    TEST_ASSERT_EQUAL_UINT(1, rig.inner.sends());
    TEST_ASSERT_TRUE(rig.menu.mailbox.idle());  // nothing was selected
    TEST_ASSERT_EQUAL_UINT(1, rig.transport.stats().menuNaks);
}

void test_drive_frames_pass_through_unchanged()
{
    DecoratorRig rig;
    const auto read = frame(0x31, 0x52, 1);
    rig.inner.push_frame(read);
    const auto received = rig.transport.waitCommandFrame(20000);
    TEST_ASSERT_TRUE(received.ok());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(read.data(), received.value().bytes.data(), 5);
    TEST_ASSERT_EQUAL_UINT(0, rig.inner.sends());
}

void test_frame_after_a_cold_reset_is_dropped_until_the_menu_is_back()
{
    DecoratorRig rig;
    rig.menu.controller->select(0);
    rig.menu.step();
    rig.menu.controller->onColdReset();  // FF seen while waiting
    rig.inner.push_frame(frame(0x31, 0x52, 1));
    const auto received = rig.transport.waitCommandFrame(20000);
    TEST_ASSERT_FALSE(received.ok());
    TEST_ASSERT_EQUAL_UINT(0, rig.inner.sends());
    TEST_ASSERT_TRUE(rig.inner.releases() >= 1);
    TEST_ASSERT_EQUAL_UINT(1, rig.transport.stats().droppedAfterReset);
    rig.menu.controller->service();
    rig.menu.step();
    TEST_ASSERT_TRUE(rig.menu.controller->menuOnD1());
    rig.inner.push_frame(frame(0x31, 0x52, 1));
    TEST_ASSERT_TRUE(rig.transport.waitCommandFrame(20000).ok());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_catalog_keeps_only_atr_names_in_case_insensitive_order);
    RUN_TEST(test_catalog_keeps_the_first_twenty_whatever_the_listing_order);
    RUN_TEST(test_catalog_skips_names_the_repository_could_not_open);
    RUN_TEST(test_read_catalog_lists_the_demo_folder);
    RUN_TEST(test_read_catalog_fails_on_a_missing_folder);
    RUN_TEST(test_menu_disk_is_a_valid_single_density_atr_with_the_catalog);
    RUN_TEST(test_menu_disk_cuts_long_names_and_masks_odd_characters);
    RUN_TEST(test_menu_disk_reads_are_all_or_nothing);
    RUN_TEST(test_start_mounts_the_menu_on_d1);
    RUN_TEST(test_valid_choice_mounts_that_image_and_closes_the_menu);
    RUN_TEST(test_invalid_image_is_refused_with_a_reason_and_nothing_is_mounted);
    RUN_TEST(test_index_outside_the_list_is_refused);
    RUN_TEST(test_cold_reset_brings_the_menu_back_and_closes_the_image);
    RUN_TEST(test_cold_reset_with_the_menu_on_d1_changes_nothing);
    RUN_TEST(test_choice_while_a_mount_is_in_flight_is_refused);
    RUN_TEST(test_menu_command_gets_ack_then_complete_with_41_bytes_and_checksum);
    RUN_TEST(test_other_menu_commands_are_nacked_and_bad_checksums_ignored);
    RUN_TEST(test_drive_frames_pass_through_unchanged);
    RUN_TEST(test_frame_after_a_cold_reset_is_dropped_until_the_menu_is_back);
    return UNITY_END();
}
