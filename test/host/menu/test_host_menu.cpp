// Menu of retrobridge_host (Issues #38 and #40, ADR-017): catalog, configured
// sources, in-memory menu disk, controller (main screen, list of the local
// source, drive choice, unmount, boot of D1, return on cold reset) and the
// transport decorator that serves the menu device. The demo images are
// generated at test time by tools/make_atr.py --demo (fixture menu_images).

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

#include "fakes/fake_clock.hpp"
#include "fakes/scripted_sio_transport.hpp"
#include "rb/menu/host_image_repository.hpp"
#include "rb/menu/menu_catalog.hpp"
#include "rb/menu/menu_controller.hpp"
#include "rb/menu/menu_image.hpp"
#include "rb/menu/menu_sio_transport.hpp"
#include "rb/menu/menu_sources.hpp"
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
using rb::host::MenuSources;

namespace {

// Atari keyboard codes.
constexpr uint8_t K1 = 0x1F, K2 = 0x1E, K3 = 0x1A, K4 = 0x18, K5 = 0x1D;
constexpr uint8_t KA = 0x3F, KB = 0x15, KC = 0x12, KD = 0x3A, KE = 0x2A, KU = 0x0B, KESC = 0x1C;
constexpr uint8_t kShift = 0x40;

DriveNumber drive(unsigned n) { return DriveNumber::from(static_cast<uint8_t>(n)).value(); }

std::array<uint8_t, 40> line(const char* text)
{
    std::array<uint8_t, 40> out{};
    rb::host::screenLine(text, Span<uint8_t>{out});
    return out;
}

std::array<uint8_t, 40> textLine(const char* text)
{
    std::array<uint8_t, 40> out{};
    rb::host::screenText(text, Span<uint8_t>{out});
    return out;
}

std::array<uint8_t, 5> frame(uint8_t device, uint8_t command, uint8_t aux1 = 0, uint8_t aux2 = 0)
{
    std::array<uint8_t, 5> bytes{device, command, aux1, aux2, 0};
    bytes[4] = rb::sio::sio_checksum(Span<const uint8_t>{bytes.data(), 4});
    return bytes;
}

bool readSources(MenuSources& sources, const char* text, char* error = nullptr)
{
    std::FILE* file = std::tmpfile();
    TEST_ASSERT_NOT_NULL(file);
    std::fputs(text, file);
    std::rewind(file);
    char scratch[96];
    const bool ok = sources.read(file, error != nullptr ? error : scratch, sizeof scratch);
    std::fclose(file);
    return ok;
}

// The --images wiring of main.cpp, minus NetSIO: the demo folder, two configured
// sources, the menu, eight drives and the controller. step() plays the start of
// SioSession::step() followed by main's afterStep().
struct MenuRig {
    MenuCatalog catalog;
    MenuSources sources;
    MenuImage menu;
    std::optional<rb::host::HostImageRepository> repository;
    rb::app::DriveBay bay{8};
    rb::app::InlineDriveMailbox mailbox;
    rb::app::ApplyDriveRequest apply{bay, mailbox};
    std::optional<rb::host::MenuController> controller;

    MenuRig()
    {
        TEST_ASSERT_TRUE(rb::host::readCatalog(RB_TEST_MENU_DIR, catalog));
        TEST_ASSERT_TRUE(readSources(sources, "tnfs.example.cl\nlocalhost\n"));
        repository.emplace(RB_TEST_MENU_DIR, menu);
        controller.emplace(catalog, sources, *repository, mailbox, nullptr);
        controller->onColdReset();
        controller->service();
        step();
    }

    void step()
    {
        apply.execute();
        controller->service();
    }

    MenuReply press(uint8_t code) { return controller->key(code); }

    // Presses keys, running a step after each one, as the Atari's round trips do.
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
};

void assert_line(const MenuReply& reply, unsigned offset, const std::array<uint8_t, 40>& expected)
{
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), reply.screen() + offset, 40);
}

unsigned driveLine(unsigned n) { return rb::host::kMainDrives + (n - 1) * 40; }
unsigned sourceLine(unsigned n) { return rb::host::kMainSources + (n - 1) * 40; }

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

void test_catalog_keeps_25_names_whatever_the_listing_order()
{
    MenuCatalog catalog;
    char name[16];
    for (int i = 24; i >= 0; --i) {
        std::snprintf(name, sizeof name, "n%02d.atr", i);
        catalog.offer(name);
    }
    TEST_ASSERT_EQUAL_UINT(25, catalog.count());  // menu-navegacion N002: up to 999, no longer 20
    TEST_ASSERT_EQUAL_UINT(25, catalog.atrSeen());
    TEST_ASSERT_EQUAL_STRING("n00.atr", catalog.name(0));
    TEST_ASSERT_EQUAL_STRING("n24.atr", catalog.name(24));
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

// --- Sources -------------------------------------------------------------------

void test_sources_skip_blanks_and_comments_and_trim()
{
    MenuSources sources;
    TEST_ASSERT_TRUE(readSources(sources, "# servers\n\n  tnfs.example.cl  \r\nlocalhost\n"));
    TEST_ASSERT_EQUAL_UINT(2, sources.count());
    TEST_ASSERT_EQUAL_STRING("tnfs.example.cl", sources.name(2));
    TEST_ASSERT_EQUAL_STRING("localhost", sources.name(3));
    TEST_ASSERT_NULL(sources.name(4));
    TEST_ASSERT_NULL(sources.name(1));  // source 1 is always the local one
}

void test_sources_refuse_an_eighth_a_long_name_and_odd_bytes()
{
    MenuSources sources;
    char error[96];
    TEST_ASSERT_FALSE(readSources(sources, "a\nb\nc\nd\ne\nf\ng\nh\n", error));
    TEST_ASSERT_EQUAL_UINT(0, sources.count());
    TEST_ASSERT_NOT_NULL(std::strstr(error, "more than 7"));
    TEST_ASSERT_TRUE(readSources(sources, (std::string(32, 'x') + "\n").c_str()));
    TEST_ASSERT_FALSE(readSources(sources, (std::string(33, 'x') + "\n").c_str(), error));
    TEST_ASSERT_NOT_NULL(std::strstr(error, "line 1"));
    TEST_ASSERT_FALSE(readSources(sources, "ok\nma\xC3\xB1" "ana\n", error));
    TEST_ASSERT_NOT_NULL(std::strstr(error, "line 2"));
    TEST_ASSERT_FALSE(readSources(sources, (std::string(300, 'y') + "\n").c_str(), error));
}

// --- Menu disk -------------------------------------------------------------------

void test_menu_disk_is_a_valid_single_density_atr_with_the_program()
{
    MenuImage menu;
    auto image = rb::domain::DiskImage::open(menu);
    TEST_ASSERT_TRUE(image.ok());
    TEST_ASSERT_EQUAL_UINT16(720, image.value().geometry().sectorCount());
    std::array<uint8_t, 128> sector{};
    TEST_ASSERT_TRUE(image.value().readSector(rb::domain::SectorNumber::from(1).value(), Span<uint8_t>{sector}).ok());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MenuImage::block().data(), sector.data(), 128);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLoadSectors, sector[1]);
    TEST_ASSERT_TRUE(image.value().readSector(rb::domain::SectorNumber::from(720).value(), Span<uint8_t>{sector}).ok());
    for (uint8_t byte : sector) {
        TEST_ASSERT_EQUAL_UINT8(0, byte);
    }
}

void test_menu_disk_reads_are_all_or_nothing()
{
    MenuImage menu;
    std::array<uint8_t, 4> out{};
    TEST_ASSERT_TRUE(menu.readAt(rb::host::kMenuImageSize - 4, Span<uint8_t>{out}).ok());
    const auto past = menu.readAt(rb::host::kMenuImageSize - 3, Span<uint8_t>{out});
    TEST_ASSERT_FALSE(past.ok());
    TEST_ASSERT_EQUAL(ErrorCode::OutOfBounds, past.error());
}

void test_screen_lines_mask_odd_characters_and_choose_the_case()
{
    std::array<uint8_t, 3> codes{};
    rb::host::screenLine("a\x7F~", Span<uint8_t>{codes});
    TEST_ASSERT_EQUAL_UINT8('A' - 0x20, codes[0]);
    TEST_ASSERT_EQUAL_UINT8('?' - 0x20, codes[1]);
    TEST_ASSERT_EQUAL_UINT8('~', codes[2]);
    rb::host::screenText("a", Span<uint8_t>{codes});
    TEST_ASSERT_EQUAL_UINT8('a', codes[0]);
    TEST_ASSERT_EQUAL_UINT8(0, codes[1]);
}

// --- Main screen -------------------------------------------------------------------

void test_main_screen_shows_sources_drives_tape_and_the_reference_frame()
{
    MenuRig rig;
    const MenuReply reply = rig.press(rb::host::kMenuNoKey);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, reply.action());
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, reply.layout());
    std::array<uint8_t, 40> first = textLine("1  SD");  // the cursor (menu-navegacion N004)
    for (uint8_t& b : first) {
        b = static_cast<uint8_t>(b | 0x80);
    }
    assert_line(reply, sourceLine(1), first);
    assert_line(reply, sourceLine(2), textLine("2  tnfs.example.cl"));  // case kept, as in the reference
    assert_line(reply, sourceLine(3), textLine("3  localhost"));
    assert_line(reply, sourceLine(4), textLine("4"));
    assert_line(reply, sourceLine(8), textLine("8"));
    for (unsigned n = 1; n <= 8; ++n) {
        char text[16];
        std::snprintf(text, sizeof text, "D%u SIN DISCO", n);
        assert_line(reply, driveLine(n), line(text));
    }
    assert_line(reply, rb::host::kMainTape, line("C: SIN CINTA"));
    assert_line(reply, rb::host::kMainMessage, line(""));
    // Title, subtitle and footer come from the generated template, untouched.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rb::host::kMenuMainTemplate, reply.screen(), rb::host::kMainSources);
    const unsigned footer = rb::host::kMainFooter;
    TEST_ASSERT_EQUAL_UINT(rb::host::kMainFooter + 80, rb::host::kMainMessage);  // message under the footer
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rb::host::kMenuMainTemplate + footer, reply.screen() + footer, 80);
    const unsigned subtitle = rb::host::kMainSources + 8 * 40;
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rb::host::kMenuMainTemplate + subtitle, reply.screen() + subtitle, 20);
}

void test_servers_and_edit_say_what_is_missing_and_change_nothing()
{
    MenuRig rig;
    assert_line(rig.keys({K2}), rb::host::kMainMessage, line(" PENDIENTE: FALTA EL CLIENTE TNFS"));
    assert_line(rig.keys({K4}), rb::host::kMainMessage, line(" FUENTE 4 SIN CONFIGURAR"));
    const MenuReply edit = rig.keys({KE});
    // Without --sources there is no file to save to (menu-editar-servidor; edit tests in test_menu_editar).
    assert_line(edit, rb::host::kMainMessage, line(" SIN ARCHIVO DE FUENTES"));
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, edit.action());
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, edit.layout());
    TEST_ASSERT_TRUE(rig.mailbox.idle());
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());
}

void test_boot_without_an_image_on_d1_says_so()
{
    MenuRig rig;
    const MenuReply reply = rig.keys({KA});
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, reply.action());
    assert_line(reply, rb::host::kMainMessage, line(" D1 SIN DISCO: ABRE LA FUENTE 1"));
}

// --- Local source ------------------------------------------------------------------

void test_source_1_lists_the_folder_and_esc_goes_back()
{
    MenuRig rig;
    const MenuReply list = rig.keys({K1});
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutList, list.layout());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(line(" RETROBRIDGE CHILE").data(), list.screen() + rb::host::kListTitle, 20);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(line(" FUENTE 1: SD").data(), list.screen() + rb::host::kListPath, 40);  // N008
    std::array<uint8_t, 40> marked = line(" A  DEMO-DOS.ATR");  // the cursor (menu-navegacion)
    for (uint8_t& b : marked) {
        b = static_cast<uint8_t>(b | 0x80);
    }
    assert_line(list, rb::host::kListEntries, marked);
    assert_line(list, rb::host::kListEntries + 40, line(" B  DEMO-UNO.ATR"));
    assert_line(list, rb::host::kListEntries + 80, line(" C  ROTA.ATR"));
    assert_line(list, rb::host::kListEntries + 120, line(""));
    assert_line(list, rb::host::kListMessage, line(" PULSA LA LETRA DE LA IMAGEN"));
    const unsigned footer = rb::host::kListFooter;
    TEST_ASSERT_EQUAL_UINT(rb::host::kListFooter + 40, rb::host::kListMessage);  // message under the footer
    TEST_ASSERT_EQUAL_UINT8_ARRAY(rb::host::kMenuListTemplate + footer, list.screen() + footer, 40);
    // A letter outside the list does nothing.
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutList, rig.keys({KD}).layout());
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, rig.keys({KESC}).layout());
}

void test_image_mounted_on_d2_appears_on_d2_and_is_served_there()
{
    MenuRig rig;
    const MenuReply pick = rig.keys({K1, KB});
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, pick.layout());  // the drives are visible
    assert_line(pick, rb::host::kMainMessage, line(" UNIDAD PARA: demo-uno.atr"));  // menu-navegacion M42-4
    TEST_ASSERT_TRUE(rig.mailbox.idle());
    const MenuReply mounted = rig.keys({K2});
    assert_line(mounted, driveLine(2), line("D2 DEMO-UNO.ATR"));
    assert_line(mounted, rb::host::kMainMessage, line(" D2: DEMO-UNO.ATR"));
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.mounted(2));
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());  // D1 still serves the menu
    TEST_ASSERT_EQUAL_UINT(1, rig.repository->openFiles());
}

void test_invalid_image_is_refused_and_the_drive_keeps_what_it_had()
{
    MenuRig rig;
    rig.keys({K1, KB, K3});
    const MenuReply refused = rig.keys({K1, KC, K3});
    assert_line(refused, rb::host::kMainMessage, line(" ERROR: ATR INVALIDA"));
    assert_line(refused, driveLine(3), line("D3 DEMO-UNO.ATR"));
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.mounted(3));
    TEST_ASSERT_EQUAL_UINT(1, rig.repository->openFiles());
}

void test_esc_while_choosing_the_drive_goes_back_to_the_list()
{
    MenuRig rig;
    const MenuReply back = rig.keys({K1, KA, KESC});
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutList, back.layout());
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, rig.keys({KESC}).layout());
    TEST_ASSERT_NULL(rig.controller->driveName(1));
}

void test_d1_choice_is_assigned_and_booted_with_a()
{
    MenuRig rig;
    const MenuReply assigned = rig.keys({K1, KA, K1});
    assert_line(assigned, driveLine(1), line("D1 DEMO-DOS.ATR"));
    assert_line(assigned, rb::host::kMainMessage, line(" D1: DEMO-DOS.ATR (A ARRANCA)"));
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());  // the menu keeps D1 until A
    TEST_ASSERT_EQUAL_STRING(rb::host::kMenuLocation, rig.mounted(1));
    TEST_ASSERT_EQUAL_UINT(0, rig.repository->openFiles());  // validated and closed

    const MenuReply boot = rig.press(KA);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionBoot, boot.action());
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());  // applied only at the next step
    rig.step();
    TEST_ASSERT_FALSE(rig.controller->menuOnD1());
    TEST_ASSERT_EQUAL_STRING("demo-dos.atr", rig.mounted(1));
    TEST_ASSERT_FALSE(rig.repository->menuOpen());
}

void test_invalid_image_is_not_assigned_to_d1()
{
    MenuRig rig;
    const MenuReply refused = rig.keys({K1, KC, K1});
    assert_line(refused, rb::host::kMainMessage, line(" ERROR: ATR INVALIDA"));
    assert_line(refused, driveLine(1), line("D1 SIN DISCO"));
    TEST_ASSERT_EQUAL_UINT(0, rig.repository->openFiles());
}

void test_cold_reset_brings_the_menu_back_and_keeps_the_drives()
{
    MenuRig rig;
    rig.keys({K1, KB, K2, K1, KA, K1});
    rig.press(KA);
    rig.step();
    TEST_ASSERT_EQUAL_STRING("demo-dos.atr", rig.mounted(1));
    rig.controller->onColdReset();
    rig.controller->service();  // main's afterStep: the menu is requested
    TEST_ASSERT_FALSE(rig.controller->coldResetPending());
    rig.step();
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());
    TEST_ASSERT_EQUAL_STRING(rb::host::kMenuLocation, rig.mounted(1));
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.mounted(2));
    const MenuReply main = rig.press(rb::host::kMenuNoKey);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, main.layout());
    assert_line(main, driveLine(1), line("D1 DEMO-DOS.ATR"));
    assert_line(main, driveLine(2), line("D2 DEMO-UNO.ATR"));
    TEST_ASSERT_EQUAL_UINT(1, rig.repository->openFiles());  // D2 only: D1's image was closed
    // Second walk: the other image boots.
    rig.keys({K1, KB, K1});
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionBoot, rig.press(KA).action());
    rig.step();
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.mounted(1));
}

void test_cold_reset_in_the_list_returns_to_the_main_screen()
{
    MenuRig rig;
    rig.keys({K1, KA});
    rig.controller->onColdReset();
    rig.controller->service();
    const MenuReply main = rig.press(rb::host::kMenuNoKey);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, main.layout());
    assert_line(main, rb::host::kMainMessage, line(""));
}

void test_u_unmounts_the_chosen_drive()
{
    MenuRig rig;
    rig.keys({K1, KB, K2, K1, KA, K1});
    assert_line(rig.keys({KU}), rb::host::kMainMessage, line(" DESMONTAR: PULSA 1-8, ESC CANCELA"));
    const MenuReply removed = rig.keys({K2});
    assert_line(removed, driveLine(2), line("D2 SIN DISCO"));
    assert_line(removed, rb::host::kMainMessage, line(" D2 DESMONTADA"));
    TEST_ASSERT_EQUAL_STRING("", rig.mounted(2));
    TEST_ASSERT_EQUAL_UINT(0, rig.repository->openFiles());
    assert_line(rig.keys({KU, K5}), rb::host::kMainMessage, line(" D5 YA ESTA VACIA"));
    const MenuReply d1 = rig.keys({KU, K1});
    assert_line(d1, driveLine(1), line("D1 SIN DISCO"));
    TEST_ASSERT_TRUE(rig.controller->menuOnD1());
    assert_line(rig.keys({KU, KESC}), rb::host::kMainMessage, line(""));
}

void test_second_request_while_one_is_in_flight_is_refused()
{
    MenuRig rig;
    rig.keys({K1, KB});
    rig.press(K2);  // no step: the mount is still in the mailbox
    rig.press(K1);
    rig.press(KA);
    const MenuReply busy = rig.press(K3);
    assert_line(busy, rb::host::kMainMessage, line(" ERROR: UNIDAD OCUPADA, REPITE"));
    assert_line(busy, driveLine(3), line("D3 SIN DISCO"));
    rig.step();
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.mounted(2));
}

void test_shift_and_control_do_not_change_a_key()
{
    MenuRig rig;
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutList, rig.keys({K1 | kShift}).layout());
    rig.keys({KB | 0x80, K2 | kShift});
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.mounted(2));
}

// --- Transport decorator -------------------------------------------------------

struct DecoratorRig {
    MenuRig menu;
    rb::test::FakeClock clock;
    rb::test::ScriptedSioTransport inner{clock};
    rb::host::MenuSioTransport transport{inner, clock, *menu.controller};
};

void test_key_command_gets_ack_then_the_screen_in_blocks_with_checksum()
{
    DecoratorRig rig;
    rig.inner.push_frame(frame(0x5A, 0x4B, K1));
    const auto received = rig.transport.waitCommandFrame(20000);
    TEST_ASSERT_FALSE(received.ok());
    TEST_ASSERT_EQUAL(ErrorCode::Timeout, received.error());

    TEST_ASSERT_EQUAL_UINT(5, rig.inner.sends());  // ACK + 964 bytes in blocks of 256
    TEST_ASSERT_EQUAL_UINT(1, rig.inner.sent(0).length);
    TEST_ASSERT_EQUAL_UINT8(0x41, rig.inner.sent(0).bytes[0]);
    std::array<uint8_t, 1 + MenuReply::kSize + 1> stream{};
    std::size_t at = 0;
    for (unsigned i = 1; i < 5; ++i) {
        const auto& block = rig.inner.sent(i);
        TEST_ASSERT_EQUAL_UINT(i < 4 ? 256 : 964 - 3 * 256, block.length);
        std::memcpy(stream.data() + at, block.bytes.data(), block.length);
        at += block.length;
    }
    TEST_ASSERT_EQUAL_UINT(stream.size(), at);
    TEST_ASSERT_EQUAL_UINT8(0x43, stream[0]);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, stream[1]);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutList, stream[2]);  // the key opened source 1
    TEST_ASSERT_EQUAL_UINT8(rb::sio::sio_checksum(Span<const uint8_t>{stream.data() + 1, MenuReply::kSize}),
                            stream[stream.size() - 1]);
    TEST_ASSERT_TRUE(rig.inner.sent(1).startUs - rig.inner.sent(0).endUs >= 250);
    TEST_ASSERT_TRUE(rig.inner.releases() >= 1);
    TEST_ASSERT_EQUAL_UINT(1, rig.transport.stats().menuCommands);
}

// Collects the 964 bytes of one reply (sends first..first+3).
std::array<uint8_t, 964> replyStream(const rb::test::ScriptedSioTransport& inner, unsigned first)
{
    std::array<uint8_t, 964> stream{};
    std::size_t at = 0;
    for (unsigned i = first; i < first + 4; ++i) {
        std::memcpy(stream.data() + at, inner.sent(i).bytes.data(), inner.sent(i).length);
        at += inner.sent(i).length;
    }
    TEST_ASSERT_EQUAL_UINT(964, at);
    return stream;
}

void test_a_key_repeated_by_siov_is_answered_again_but_applied_once()
{
    DecoratorRig rig;
    rig.menu.keys({K1, KB});  // list, image B chosen: the next digit is the drive
    rig.inner.push_frame(frame(0x5A, 0x4B, K2, 7));
    TEST_ASSERT_FALSE(rig.transport.waitCommandFrame(20000).ok());
    rig.menu.step();
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.menu.mounted(2));
    const auto first = replyStream(rig.inner, 1);
    // The reply was lost: SIOV sends the same frame again (same key, same AUX2).
    rig.inner.push_frame(frame(0x5A, 0x4B, K2, 7));
    TEST_ASSERT_FALSE(rig.transport.waitCommandFrame(20000).ok());
    rig.menu.step();
    const auto again = replyStream(rig.inner, 6);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first.data(), again.data(), first.size());
    TEST_ASSERT_EQUAL_UINT(1, rig.transport.stats().menuRetries);
    // Not read as "2" on the main screen (that would show the TNFS message).
    assert_line(rig.menu.press(rb::host::kMenuNoKey), rb::host::kMainMessage, line(" D2: DEMO-UNO.ATR"));
    // The same key with the next number is a new key.
    rig.inner.push_frame(frame(0x5A, 0x4B, K2, 8));
    TEST_ASSERT_FALSE(rig.transport.waitCommandFrame(20000).ok());
    assert_line(rig.menu.press(rb::host::kMenuNoKey), rb::host::kMainMessage,
                line(" PENDIENTE: FALTA EL CLIENTE TNFS"));
    TEST_ASSERT_EQUAL_UINT(1, rig.transport.stats().menuRetries);
}

void test_a_redraw_forgets_the_last_key()
{
    DecoratorRig rig;
    rig.inner.push_frame(frame(0x5A, 0x4B, K2, 2));
    rig.inner.push_frame(frame(0x5A, 0x4B, rb::host::kMenuNoKey, 1));  // the menu started again
    rig.inner.push_frame(frame(0x5A, 0x4B, K2, 2));
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_FALSE(rig.transport.waitCommandFrame(20000).ok());
    }
    TEST_ASSERT_EQUAL_UINT(0, rig.transport.stats().menuRetries);
    TEST_ASSERT_EQUAL_UINT(3, rig.transport.stats().menuCommands);
}

// AUX2 bits 4-0 carry the row the Atari's bar shows: RETURN takes that row, not the
// one the host drew last (bits 7-5 stay the sequence number).
void test_the_row_in_aux2_reaches_the_controller()
{
    DecoratorRig rig;
    const MenuReply list = rig.menu.keys({K1});  // the local list; the host on row 0 (demo-dos.atr)
    TEST_ASSERT_EQUAL_UINT8(0, list.screen()[rb::host::kMenuCursor + 3]);
    rig.inner.push_frame(frame(0x5A, 0x4B, 0x0C, 3 << 5 | 2));  // RETURN, the bar on row 2
    TEST_ASSERT_FALSE(rig.transport.waitCommandFrame(20000).ok());
    assert_line(rig.menu.press(rb::host::kMenuNoKey), rb::host::kMainMessage, line(" UNIDAD PARA: rota.atr"));
}

// Without a row (kMenuNoRow: no local bar on the Atari) the host keeps its own row.
void test_no_row_keeps_the_row_of_the_host()
{
    MenuRig rig;
    // A down that reaches the host is an edge move: it starts from the last row (rota.atr).
    const MenuReply moved = rig.keys({K1, rb::host::kMenuNavDown});
    TEST_ASSERT_EQUAL_UINT8(2, moved.screen()[rb::host::kMenuCursor + 3]);
    assert_line(rig.press(0x0C), rb::host::kMainMessage, line(" UNIDAD PARA: rota.atr"));
}

void test_a_cold_reset_forgets_the_last_key_too()
{
    DecoratorRig rig;
    rig.inner.push_frame(frame(0x5A, 0x4B, K2, 2));
    TEST_ASSERT_FALSE(rig.transport.waitCommandFrame(20000).ok());
    rig.menu.controller->onColdReset();
    rig.inner.push_frame(frame(0x31, 0x53));  // dropped: the menu is not back yet
    TEST_ASSERT_FALSE(rig.transport.waitCommandFrame(20000).ok());
    rig.menu.controller->service();
    rig.menu.step();
    rig.inner.push_frame(frame(0x5A, 0x4B, K2, 2));  // same bytes, but a new key of the new menu
    TEST_ASSERT_FALSE(rig.transport.waitCommandFrame(20000).ok());
    TEST_ASSERT_EQUAL_UINT(0, rig.transport.stats().menuRetries);
    TEST_ASSERT_EQUAL_UINT(2, rig.transport.stats().menuCommands);
}

void test_other_menu_commands_are_nacked_and_bad_checksums_ignored()
{
    DecoratorRig rig;
    rig.inner.push_frame(frame(0x5A, 0x4D, 0));  // Issue #38's 'M' is gone
    auto bad = frame(0x5A, 0x4B, K1);
    bad[4] ^= 0xFF;
    rig.inner.push_frame(bad);
    TEST_ASSERT_FALSE(rig.transport.waitCommandFrame(20000).ok());
    TEST_ASSERT_EQUAL_UINT(1, rig.inner.sends());
    TEST_ASSERT_EQUAL_UINT8(0x4E, rig.inner.sent(0).bytes[0]);
    TEST_ASSERT_FALSE(rig.transport.waitCommandFrame(20000).ok());
    TEST_ASSERT_EQUAL_UINT(1, rig.inner.sends());
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, rig.menu.press(rb::host::kMenuNoKey).layout());
    TEST_ASSERT_EQUAL_UINT(1, rig.transport.stats().menuNaks);
}

void test_drive_frames_pass_through_unchanged()
{
    DecoratorRig rig;
    const auto read = frame(0x32, 0x52, 1);
    rig.inner.push_frame(read);
    const auto received = rig.transport.waitCommandFrame(20000);
    TEST_ASSERT_TRUE(received.ok());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(read.data(), received.value().bytes.data(), 5);
    TEST_ASSERT_EQUAL_UINT(0, rig.inner.sends());
}

void test_frame_after_a_cold_reset_is_dropped_until_the_menu_is_back()
{
    DecoratorRig rig;
    rig.menu.keys({K1, KA, K1});
    rig.menu.press(KA);
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
    RUN_TEST(test_catalog_keeps_25_names_whatever_the_listing_order);
    RUN_TEST(test_catalog_skips_names_the_repository_could_not_open);
    RUN_TEST(test_read_catalog_lists_the_demo_folder);
    RUN_TEST(test_read_catalog_fails_on_a_missing_folder);
    RUN_TEST(test_sources_skip_blanks_and_comments_and_trim);
    RUN_TEST(test_sources_refuse_an_eighth_a_long_name_and_odd_bytes);
    RUN_TEST(test_menu_disk_is_a_valid_single_density_atr_with_the_program);
    RUN_TEST(test_menu_disk_reads_are_all_or_nothing);
    RUN_TEST(test_screen_lines_mask_odd_characters_and_choose_the_case);
    RUN_TEST(test_main_screen_shows_sources_drives_tape_and_the_reference_frame);
    RUN_TEST(test_servers_and_edit_say_what_is_missing_and_change_nothing);
    RUN_TEST(test_boot_without_an_image_on_d1_says_so);
    RUN_TEST(test_source_1_lists_the_folder_and_esc_goes_back);
    RUN_TEST(test_image_mounted_on_d2_appears_on_d2_and_is_served_there);
    RUN_TEST(test_invalid_image_is_refused_and_the_drive_keeps_what_it_had);
    RUN_TEST(test_esc_while_choosing_the_drive_goes_back_to_the_list);
    RUN_TEST(test_d1_choice_is_assigned_and_booted_with_a);
    RUN_TEST(test_invalid_image_is_not_assigned_to_d1);
    RUN_TEST(test_cold_reset_brings_the_menu_back_and_keeps_the_drives);
    RUN_TEST(test_cold_reset_in_the_list_returns_to_the_main_screen);
    RUN_TEST(test_u_unmounts_the_chosen_drive);
    RUN_TEST(test_second_request_while_one_is_in_flight_is_refused);
    RUN_TEST(test_shift_and_control_do_not_change_a_key);
    RUN_TEST(test_key_command_gets_ack_then_the_screen_in_blocks_with_checksum);
    RUN_TEST(test_a_key_repeated_by_siov_is_answered_again_but_applied_once);
    RUN_TEST(test_a_redraw_forgets_the_last_key);
    RUN_TEST(test_the_row_in_aux2_reaches_the_controller);
    RUN_TEST(test_no_row_keeps_the_row_of_the_host);
    RUN_TEST(test_a_cold_reset_forgets_the_last_key_too);
    RUN_TEST(test_other_menu_commands_are_nacked_and_bad_checksums_ignored);
    RUN_TEST(test_drive_frames_pass_through_unchanged);
    RUN_TEST(test_frame_after_a_cold_reset_is_dropped_until_the_menu_is_back);
    return UNITY_END();
}
