// Host tests for E on the main screen (spec menu-editar-servidor, M43): editing
// or adding the TNFS server of a source and rewriting the --sources file.
// The 6502 side (the field, the keys, the 'T' commands) is tested in
// tools/tests/test_menu_program.py; here the host gets what the 6502 sends.

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
#include "rb/sio/sio_checksum.hpp"
#include "unity.h"

namespace {

using rb::core::Span;
using rb::host::MenuCatalog;
using rb::host::MenuController;
using rb::host::MenuImage;
using rb::host::MenuReply;
using rb::host::MenuSources;

constexpr uint8_t K2 = 0x1E, KE = 0x2A, KESC = 0x1C;
constexpr const char* kFile = RB_TEST_MENU_DIR "/sources-edit.txt";
constexpr const char* kOriginal = "# servidores del laboratorio\n\ntnfs.example.cl\n  # otro\nlocalhost\n";

std::string readFile(const char* path)
{
    std::string text;
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return "<none>";
    }
    char buffer[256];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof buffer, file)) > 0) {
        text.append(buffer, got);
    }
    std::fclose(file);
    return text;
}

void writeFile(const char* path, const char* text)
{
    std::FILE* file = std::fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(file);
    std::fputs(text, file);
    std::fclose(file);
}

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

void assert_line(const MenuReply& reply, unsigned offset, const std::array<uint8_t, 40>& expected)
{
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), reply.screen() + offset, 40);
}

unsigned sourceLine(unsigned n) { return rb::host::kMainSources + (n - 1) * 40; }

struct Events final : rb::host::MenuEvents {
    void menuEvent(const char* name, const char* detail) override
    {
        last = std::string(name) + " | " + detail;
    }
    std::string last;
};

// The --images wiring of main.cpp with --sources kFile (two sources), no TNFS loader.
struct EditRig {
    MenuCatalog catalog;
    MenuSources sources;
    MenuImage menu;
    std::optional<rb::host::HostImageRepository> repository;
    rb::app::DriveBay bay{8};
    rb::app::InlineDriveMailbox mailbox;
    rb::app::ApplyDriveRequest apply{bay, mailbox};
    Events events;
    std::optional<MenuController> controller;

    explicit EditRig(const char* file = kFile, const char* text = kOriginal)
    {
        TEST_ASSERT_TRUE(rb::host::readCatalog(RB_TEST_MENU_DIR, catalog));
        if (text != nullptr) {
            writeFile(kFile, text);
        }
        std::FILE* in = std::fopen(kFile, "rb");
        TEST_ASSERT_NOT_NULL(in);
        char error[96];
        TEST_ASSERT_TRUE(sources.read(in, error, sizeof error));
        std::fclose(in);
        repository.emplace(RB_TEST_MENU_DIR, menu);
        controller.emplace(catalog, sources, *repository, mailbox, &events);
        controller->setSourcesFile(file);
        controller->onColdReset();
        controller->service();
        apply.execute();
        controller->service();
    }

    // E with the Atari's bar on `row` (0: source 1), as AUX2 bits 4-0 bring it.
    MenuReply pressE(unsigned row) { return controller->key(KE, static_cast<uint8_t>(row)); }

    // What the 6502 sends on RETURN: the 32 positions of the field, then kMenuEditSave.
    MenuReply type(const char* text)
    {
        const std::size_t length = std::strlen(text);
        for (uint8_t i = 0; i < rb::host::kMenuEditWidth; ++i) {
            controller->text(i, static_cast<uint8_t>(i < length ? text[i] : ' '));
        }
        return controller->key(rb::host::kMenuEditSave);
    }
};

unsigned fieldOf(const MenuReply& reply)
{
    const uint8_t* block = reply.screen() + rb::host::kMenuEdit;
    return block[0] | (block[1] << 8);
}

}  // namespace

void setUp() {}
void tearDown() { std::remove(kFile); }

// --- M43-1: what a server name is -------------------------------------------------

void test_names_and_ipv4_addresses_are_told_apart()
{
    const char* good[] = {"pulgarcito", "tnfs.example.cl", "192.168.1.30", "0.0.0.0", "255.255.255.255",
                          "a-b.c-d", "A1", "x", "abcdefghijklmnopqrstuvwxyz012345", "1host.cl"};
    const char* bad[] = {"",           "192.168.1.300", "192.168.01.3", "1.2.3",      "1.2.3.4.5",
                         "1..2.3",     "-host",         "host-",        "a.-b",       "a..b",
                         ".host",      "host.",         "ho st",        "host_name",  "a:16384",
                         "abcdefghijklmnopqrstuvwxyz0123456", "1.2.3.4."};
    for (const char* name : good) {
        TEST_ASSERT_TRUE_MESSAGE(MenuSources::validServer(name), name);
    }
    for (const char* name : bad) {
        TEST_ASSERT_FALSE_MESSAGE(MenuSources::validServer(name), name);
    }
}

// --- The --sources file -------------------------------------------------------------

void test_rewriting_a_source_keeps_comments_blank_lines_and_the_others()
{
    writeFile(kFile, kOriginal);
    char error[96];
    TEST_ASSERT_TRUE(rb::host::writeSourcesFile(kFile, 1, "10.0.0.7", error, sizeof error));
    TEST_ASSERT_EQUAL_STRING("# servidores del laboratorio\n\ntnfs.example.cl\n  # otro\n10.0.0.7\n",
                             readFile(kFile).c_str());
    TEST_ASSERT_TRUE(rb::host::writeSourcesFile(kFile, 0, "pulgarcito", error, sizeof error));
    TEST_ASSERT_EQUAL_STRING("# servidores del laboratorio\n\npulgarcito\n  # otro\n10.0.0.7\n",
                             readFile(kFile).c_str());
    TEST_ASSERT_EQUAL_STRING("<none>", readFile(RB_TEST_MENU_DIR "/sources-edit.txt.tmp").c_str());
}

void test_a_new_source_goes_at_the_end_even_without_a_last_newline()
{
    writeFile(kFile, "uno\r\n# fin");
    char error[96];
    TEST_ASSERT_TRUE(rb::host::writeSourcesFile(kFile, 1, "dos", error, sizeof error));
    TEST_ASSERT_EQUAL_STRING("uno\r\n# fin\ndos\n", readFile(kFile).c_str());
    writeFile(kFile, "");
    TEST_ASSERT_TRUE(rb::host::writeSourcesFile(kFile, 0, "primero", error, sizeof error));
    TEST_ASSERT_EQUAL_STRING("primero\n", readFile(kFile).c_str());
}

void test_a_file_that_cannot_be_rewritten_is_left_as_it_was()
{
    writeFile(kFile, kOriginal);
    char error[96] = {};
    // A gap: the file has 2 sources, so there is no source 5 to write.
    TEST_ASSERT_FALSE(rb::host::writeSourcesFile(kFile, 3, "x", error, sizeof error));
    TEST_ASSERT_TRUE(std::strlen(error) > 0);
    TEST_ASSERT_EQUAL_STRING(kOriginal, readFile(kFile).c_str());
    // A folder is not a file, for any user (root included).
    TEST_ASSERT_FALSE(rb::host::writeSourcesFile(RB_TEST_MENU_DIR, 0, "x", error, sizeof error));
    TEST_ASSERT_FALSE(rb::host::writeSourcesFile(RB_TEST_MENU_DIR "/no/such/file", 0, "x", error, sizeof error));
}

void test_a_missing_file_is_created_by_the_first_source_saved()
{
    constexpr const char* file = RB_TEST_MENU_DIR "/sources-created.txt";
    std::remove(file);
    char error[96];
    TEST_ASSERT_TRUE(rb::host::writeSourcesFile(file, 0, "tnfs.calaluis.cl", error, sizeof error));
    TEST_ASSERT_EQUAL_STRING("tnfs.calaluis.cl\n", readFile(file).c_str());
    std::remove(file);
    // Only source 2 can be the first: a gap or a deletion leaves no file behind.
    TEST_ASSERT_FALSE(rb::host::writeSourcesFile(file, 1, "x", error, sizeof error));
    TEST_ASSERT_FALSE(rb::host::writeSourcesFile(file, 0, nullptr, error, sizeof error));
    TEST_ASSERT_EQUAL_STRING("<none>", readFile(file).c_str());
    TEST_ASSERT_EQUAL_STRING("<none>", readFile(RB_TEST_MENU_DIR "/sources-created.txt.tmp").c_str());
}

constexpr const char* kTmp = RB_TEST_MENU_DIR "/sources-edit.txt.tmp";
constexpr const char* kBak = RB_TEST_MENU_DIR "/sources-edit.txt.bak";

int recovered() { return static_cast<int>(rb::host::recoverSourcesFile(kFile)); }
int as(rb::host::SourcesRecovery r) { return static_cast<int>(r); }

// T5 review: a save cut by a power loss leaves .tmp and/or .bak; the next start never trusts a
// .tmp and never loses the last whole file.
void test_a_cut_save_never_loses_the_last_whole_file()
{
    using rb::host::SourcesRecovery;
    std::remove(kTmp);
    std::remove(kBak);
    writeFile(kFile, kOriginal);
    TEST_ASSERT_EQUAL_INT(as(SourcesRecovery::Nothing), recovered());
    // Cut while the .tmp was written: the file stays, the .tmp goes.
    writeFile(kTmp, "a medio escr");
    TEST_ASSERT_EQUAL_INT(as(SourcesRecovery::Discarded), recovered());
    TEST_ASSERT_EQUAL_STRING(kOriginal, readFile(kFile).c_str());
    TEST_ASSERT_EQUAL_STRING("<none>", readFile(kTmp).c_str());
    // Cut after the old file became .bak: the old file comes back, the .tmp is not trusted.
    std::rename(kFile, kBak);
    writeFile(kTmp, "nuevo.cl\n");
    TEST_ASSERT_EQUAL_INT(as(SourcesRecovery::Restored), recovered());
    TEST_ASSERT_EQUAL_STRING(kOriginal, readFile(kFile).c_str());
    TEST_ASSERT_EQUAL_STRING("<none>", readFile(kTmp).c_str());
    TEST_ASSERT_EQUAL_STRING("<none>", readFile(kBak).c_str());
    // Cut after the new file got its name: it stays, the stale .bak goes.
    writeFile(kBak, "viejo.cl\n");
    TEST_ASSERT_EQUAL_INT(as(SourcesRecovery::Discarded), recovered());
    TEST_ASSERT_EQUAL_STRING(kOriginal, readFile(kFile).c_str());
    TEST_ASSERT_EQUAL_STRING("<none>", readFile(kBak).c_str());
    // A first save cut short (no file before): its .tmp, maybe cut, is not made the file.
    std::remove(kFile);
    writeFile(kTmp, "primer");
    TEST_ASSERT_EQUAL_INT(as(SourcesRecovery::Discarded), recovered());
    TEST_ASSERT_EQUAL_STRING("<none>", readFile(kFile).c_str());
    TEST_ASSERT_EQUAL_STRING("<none>", readFile(kTmp).c_str());
}

// T5 review: a rename that fails at any step of a save leaves the old file, and only it.
const char* g_failFrom = nullptr;
int failingRename(const char* from, const char* to)
{
    const std::size_t length = std::strlen(from);
    const std::size_t suffix = std::strlen(g_failFrom);
    if (length >= suffix && std::strcmp(from + length - suffix, g_failFrom) == 0) {
        return -1;
    }
    return std::rename(from, to);
}

void test_a_failed_rename_keeps_the_old_file_and_leaves_nothing_else()
{
    std::remove(kTmp);
    std::remove(kBak);
    const char* steps[] = {".txt", ".tmp"};  // moving the old file aside; naming the new one
    for (const char* step : steps) {
        writeFile(kFile, kOriginal);
        g_failFrom = step;
        rb::host::sourcesFileRename = failingRename;
        char error[96] = {};
        const bool saved = rb::host::writeSourcesFile(kFile, 0, "otro.cl", error, sizeof error);
        rb::host::sourcesFileRename = [](const char* from, const char* to) { return std::rename(from, to); };
        TEST_ASSERT_FALSE_MESSAGE(saved, step);
        TEST_ASSERT_TRUE(std::strlen(error) > 0);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(kOriginal, readFile(kFile).c_str(), step);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("<none>", readFile(kTmp).c_str(), step);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("<none>", readFile(kBak).c_str(), step);
    }
}

// T5 review: on the FujiNet the second save replaces a file that exists; both saves stick.
void test_two_saves_in_a_row_both_reach_the_file()
{
    constexpr const char* file = RB_TEST_MENU_DIR "/sources-created.txt";
    std::remove(file);
    EditRig rig{file, ""};
    rig.pressE(1);
    assert_line(rig.type("primero.cl"), rb::host::kMainMessage, line(" FUENTE 2 GUARDADA"));
    rig.pressE(1);
    assert_line(rig.type("segundo.cl"), rb::host::kMainMessage, line(" FUENTE 2 GUARDADA"));
    TEST_ASSERT_EQUAL_STRING("segundo.cl", rig.sources.name(2));
    TEST_ASSERT_EQUAL_STRING("segundo.cl\n", readFile(file).c_str());
    TEST_ASSERT_EQUAL_STRING("<none>", readFile(RB_TEST_MENU_DIR "/sources-created.txt.bak").c_str());
    TEST_ASSERT_EQUAL_STRING("<none>", readFile(RB_TEST_MENU_DIR "/sources-created.txt.tmp").c_str());
    std::remove(file);
}

// --- E on the main screen -----------------------------------------------------------

void test_e_opens_the_row_of_the_bar_with_its_name_in_the_field()
{
    EditRig rig;
    const MenuReply edit = rig.pressE(2);  // the bar on source 3
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionEdit, edit.action());
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuLayoutMain, edit.layout());
    TEST_ASSERT_EQUAL_UINT(sourceLine(3) + 3, fieldOf(edit));  // after "3  "
    TEST_ASSERT_EQUAL_UINT8(9, edit.screen()[rb::host::kMenuEdit + 2]);  // "localhost"
    assert_line(edit, sourceLine(3), textLine("3  localhost"));      // no bar while editing
    TEST_ASSERT_EQUAL_UINT8(0, edit.screen()[rb::host::kMenuCursor + 2]);  // no local cursor
    assert_line(edit, rb::host::kMainMessage, line(" RETURN GUARDA, VACIO BORRA, ESC CANCELA"));
    // An empty row opens an empty field.
    rig.controller->key(KESC);
    const MenuReply empty = rig.pressE(1 + 3);  // source 5
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionEdit, empty.action());
    TEST_ASSERT_EQUAL_UINT(sourceLine(5) + 3, fieldOf(empty));
    TEST_ASSERT_EQUAL_UINT8(0, empty.screen()[rb::host::kMenuEdit + 2]);
}

void test_return_saves_the_name_in_the_file_and_on_the_screen()
{
    EditRig rig;
    rig.pressE(2);
    const MenuReply saved = rig.type("192.168.1.30");
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, saved.action());
    assert_line(saved, rb::host::kMainMessage, line(" FUENTE 3 GUARDADA"));
    std::array<uint8_t, 40> marked = textLine("3  192.168.1.30");
    for (uint8_t& b : marked) {
        b = static_cast<uint8_t>(b | 0x80);
    }
    assert_line(saved, sourceLine(3), marked);  // the bar stays on the row edited
    TEST_ASSERT_EQUAL_STRING("192.168.1.30", rig.sources.name(3));
    TEST_ASSERT_EQUAL_STRING("# servidores del laboratorio\n\ntnfs.example.cl\n  # otro\n192.168.1.30\n",
                             readFile(kFile).c_str());
    TEST_ASSERT_EQUAL_STRING("menu_source_saved | source=3 name=192.168.1.30", rig.events.last.c_str());
}

void test_source_2_is_added_on_an_empty_list()
{
    EditRig rig{kFile, "# vacio\n"};
    rig.pressE(1);
    const MenuReply saved = rig.type("pulgarcito");
    assert_line(saved, rb::host::kMainMessage, line(" FUENTE 2 GUARDADA"));
    TEST_ASSERT_EQUAL_STRING("pulgarcito", rig.sources.name(2));
    TEST_ASSERT_EQUAL_STRING("# vacio\npulgarcito\n", readFile(kFile).c_str());
}

void test_an_empty_row_below_the_last_takes_the_first_free_number()
{
    EditRig rig;
    rig.pressE(4);  // source 5; sources 2 and 3 are configured
    const MenuReply saved = rig.type("nuevo.example.cl");
    assert_line(saved, rb::host::kMainMessage, line(" GUARDADA COMO FUENTE 4"));
    assert_line(saved, sourceLine(4), [] {
        std::array<uint8_t, 40> l = textLine("4  nuevo.example.cl");
        for (uint8_t& b : l) {
            b = static_cast<uint8_t>(b | 0x80);
        }
        return l;
    }());
    assert_line(saved, sourceLine(5), textLine("5"));
    TEST_ASSERT_EQUAL_UINT(3, rig.sources.count());
    TEST_ASSERT_EQUAL_STRING(
        "# servidores del laboratorio\n\ntnfs.example.cl\n  # otro\nlocalhost\nnuevo.example.cl\n",
        readFile(kFile).c_str());
}

void test_an_invalid_name_stays_in_the_field_and_changes_nothing()
{
    EditRig rig;
    rig.pressE(2);
    const MenuReply refused = rig.type("192.168.1.300");
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionEdit, refused.action());
    assert_line(refused, rb::host::kMainMessage, line(" NOMBRE O IPV4 NO VALIDO"));
    assert_line(refused, sourceLine(3), textLine("3  192.168.1.300"));  // the text typed, to fix it
    TEST_ASSERT_EQUAL_UINT8(13, refused.screen()[rb::host::kMenuEdit + 2]);
    TEST_ASSERT_EQUAL_STRING("localhost", rig.sources.name(3));
    TEST_ASSERT_EQUAL_STRING(kOriginal, readFile(kFile).c_str());
    // Fixed and saved.
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, rig.type("192.168.1.3").action());
    TEST_ASSERT_EQUAL_STRING("192.168.1.3", rig.sources.name(3));
}

void test_esc_and_an_empty_text_on_an_empty_row_change_nothing()
{
    EditRig rig;
    rig.pressE(1);
    rig.controller->text(0, 'z');
    const MenuReply esc = rig.controller->key(KESC);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, esc.action());
    assert_line(esc, rb::host::kMainMessage, line(""));
    rig.pressE(4);  // source 5: an empty row
    const MenuReply empty = rig.type("");
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, empty.action());
    assert_line(empty, rb::host::kMainMessage, line(""));
    TEST_ASSERT_EQUAL_UINT(2, rig.sources.count());
    TEST_ASSERT_EQUAL_STRING("tnfs.example.cl", rig.sources.name(2));
    TEST_ASSERT_EQUAL_STRING(kOriginal, readFile(kFile).c_str());
}

// --- M44: an empty text deletes a configured source ---------------------------------

void test_writing_no_name_removes_that_source_line_only()
{
    writeFile(kFile, kOriginal);
    char error[96];
    TEST_ASSERT_TRUE(rb::host::writeSourcesFile(kFile, 0, nullptr, error, sizeof error));
    TEST_ASSERT_EQUAL_STRING("# servidores del laboratorio\n\n  # otro\nlocalhost\n", readFile(kFile).c_str());
    writeFile(kFile, "uno\r\n# fin\ndos");  // the last line without a newline
    TEST_ASSERT_TRUE(rb::host::writeSourcesFile(kFile, 1, nullptr, error, sizeof error));
    TEST_ASSERT_EQUAL_STRING("uno\r\n# fin\n", readFile(kFile).c_str());
    // No such source: nothing to delete, the file is unchanged.
    TEST_ASSERT_FALSE(rb::host::writeSourcesFile(kFile, 1, nullptr, error, sizeof error));
    TEST_ASSERT_EQUAL_STRING("uno\r\n# fin\n", readFile(kFile).c_str());
}

void test_an_empty_text_deletes_the_source_and_the_next_ones_move_up()
{
    EditRig rig;
    rig.pressE(1);  // source 2: tnfs.example.cl
    const MenuReply deleted = rig.type("");
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, deleted.action());
    assert_line(deleted, rb::host::kMainMessage, line(" FUENTE 2 BORRADA"));
    TEST_ASSERT_EQUAL_UINT(1, rig.sources.count());
    TEST_ASSERT_EQUAL_STRING("localhost", rig.sources.name(2));
    TEST_ASSERT_NULL(rig.sources.name(3));
    std::array<uint8_t, 40> marked = textLine("2  localhost");
    for (uint8_t& b : marked) {
        b = static_cast<uint8_t>(b | 0x80);
    }
    assert_line(deleted, sourceLine(2), marked);  // the bar stays on the row
    assert_line(deleted, sourceLine(3), textLine("3"));
    TEST_ASSERT_EQUAL_STRING("# servidores del laboratorio\n\n  # otro\nlocalhost\n", readFile(kFile).c_str());
    TEST_ASSERT_EQUAL_STRING("menu_source_deleted | source=2 name=tnfs.example.cl", rig.events.last.c_str());
}

void test_a_delete_that_cannot_be_written_keeps_the_source()
{
    EditRig rig{RB_TEST_MENU_DIR};  // reads kFile, but must write into a folder
    rig.pressE(2);
    const MenuReply failed = rig.type("");
    assert_line(failed, rb::host::kMainMessage, line(" NO SE PUDO GUARDAR EL ARCHIVO"));
    TEST_ASSERT_EQUAL_UINT(2, rig.sources.count());
    TEST_ASSERT_EQUAL_STRING("localhost", rig.sources.name(3));
    TEST_ASSERT_EQUAL_STRING(kOriginal, readFile(kFile).c_str());
}

void test_a_save_repeated_by_siov_deletes_one_source()
{
    EditRig rig;
    rb::test::FakeClock clock;
    rb::test::ScriptedSioTransport inner{clock};
    rb::host::MenuSioTransport transport{inner, clock, *rig.controller};
    rig.pressE(1);
    for (uint8_t i = 0; i < rb::host::kMenuEditWidth; ++i) {
        rig.controller->text(i, ' ');
    }
    std::array<uint8_t, 5> f{0x5A, 0x4B, rb::host::kMenuEditSave, 3 << 5 | 1, 0};
    f[4] = rb::sio::sio_checksum(Span<const uint8_t>{f.data(), 4});
    inner.push_frame(f);
    inner.push_frame(f);  // the reply was lost: SIOV sends the same frame again
    for (unsigned i = 0; i < 2; ++i) {
        TEST_ASSERT_FALSE(transport.waitCommandFrame(20000).ok());
    }
    TEST_ASSERT_EQUAL_UINT(1, transport.stats().menuRetries);
    TEST_ASSERT_EQUAL_UINT(1, rig.sources.count());
    TEST_ASSERT_EQUAL_STRING("localhost", rig.sources.name(2));
    TEST_ASSERT_EQUAL_STRING("# servidores del laboratorio\n\n  # otro\nlocalhost\n", readFile(kFile).c_str());
}

void test_e_on_source_1_or_without_a_file_says_why()
{
    EditRig rig;
    const MenuReply local = rig.pressE(0);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, local.action());
    assert_line(local, rb::host::kMainMessage, line(" LA FUENTE 1 ES LOCAL"));
    EditRig none{nullptr};
    const MenuReply noFile = none.pressE(1);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, noFile.action());
    assert_line(noFile, rb::host::kMainMessage, line(" SIN ARCHIVO DE FUENTES"));
}

void test_a_file_that_cannot_be_written_says_so_and_keeps_the_source()
{
    EditRig rig{RB_TEST_MENU_DIR};  // reads kFile, but must write into a folder
    rig.pressE(1);
    const MenuReply failed = rig.type("otro.cl");
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, failed.action());
    assert_line(failed, rb::host::kMainMessage, line(" NO SE PUDO GUARDAR EL ARCHIVO"));
    TEST_ASSERT_EQUAL_STRING("tnfs.example.cl", rig.sources.name(2));
    TEST_ASSERT_EQUAL_STRING(kOriginal, readFile(kFile).c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, std::strncmp("menu_source_rejected | source=2 reason=", rig.events.last.c_str(), 39),
                                  rig.events.last.c_str());
}

// T5 (C6): with no sources file yet, E on the Atari creates it with the first server.
void test_e_creates_the_sources_file_when_it_is_missing()
{
    constexpr const char* file = RB_TEST_MENU_DIR "/sources-created.txt";
    std::remove(file);
    EditRig rig{file, ""};  // no source read: the file the menu edits does not exist
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionEdit, rig.pressE(1).action());
    const MenuReply saved = rig.type("tnfs.calaluis.cl");
    assert_line(saved, rb::host::kMainMessage, line(" FUENTE 2 GUARDADA"));
    TEST_ASSERT_EQUAL_STRING("tnfs.calaluis.cl", rig.sources.name(2));
    TEST_ASSERT_EQUAL_STRING("tnfs.calaluis.cl\n", readFile(file).c_str());
    std::remove(file);
}

// T5: a file that cannot be created (its folder is missing) says so; the sources stay.
void test_a_sources_file_that_cannot_be_created_says_so_and_keeps_the_sources()
{
    EditRig rig{RB_TEST_MENU_DIR "/no-such-folder/sources.txt"};
    rig.pressE(1);
    const MenuReply failed = rig.type("otro.cl");
    assert_line(failed, rb::host::kMainMessage, line(" NO SE PUDO GUARDAR EL ARCHIVO"));
    TEST_ASSERT_EQUAL_STRING("tnfs.example.cl", rig.sources.name(2));
    TEST_ASSERT_EQUAL_STRING(kOriginal, readFile(kFile).c_str());
}

// --- 'T' through the transport decorator ---------------------------------------------

void test_text_command_gets_ack_and_complete_and_a_retry_writes_the_same()
{
    EditRig rig;
    rb::test::FakeClock clock;
    rb::test::ScriptedSioTransport inner{clock};
    rb::host::MenuSioTransport transport{inner, clock, *rig.controller};
    rig.pressE(1);
    // The scripted transport keeps 16 sends: 7 characters and one retry, ACK and COMPLETE
    // each. The rest of the field is blanked directly, as the 6502 would send it.
    for (uint8_t i = 7; i < rb::host::kMenuEditWidth; ++i) {
        rig.controller->text(i, ' ');
    }
    const char* name = "otra.cl";
    for (uint8_t i = 0; i < 7; ++i) {
        std::array<uint8_t, 5> f{0x5A, rb::host::kMenuTextCommand, i, static_cast<uint8_t>(name[i]), 0};
        f[4] = rb::sio::sio_checksum(Span<const uint8_t>{f.data(), 4});
        inner.push_frame(f);
        if (i == 2) {
            inner.push_frame(f);  // SIOV's retry: same position, same character
        }
    }
    for (unsigned i = 0; i < 8; ++i) {
        TEST_ASSERT_FALSE(transport.waitCommandFrame(20000).ok());
    }
    TEST_ASSERT_EQUAL_UINT(16, inner.sends());
    TEST_ASSERT_EQUAL_UINT8(0x41, inner.sent(0).bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(0x43, inner.sent(1).bytes[0]);
    TEST_ASSERT_EQUAL_UINT(1, inner.sent(1).length);
    TEST_ASSERT_TRUE(inner.sent(1).startUs - inner.sent(0).endUs >= 250);
    TEST_ASSERT_EQUAL_UINT(8, transport.stats().textCommands);
    TEST_ASSERT_EQUAL_UINT(0, transport.stats().menuCommands);
    rig.controller->key(rb::host::kMenuEditSave);
    TEST_ASSERT_EQUAL_STRING("otra.cl", rig.sources.name(2));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_names_and_ipv4_addresses_are_told_apart);
    RUN_TEST(test_rewriting_a_source_keeps_comments_blank_lines_and_the_others);
    RUN_TEST(test_a_new_source_goes_at_the_end_even_without_a_last_newline);
    RUN_TEST(test_a_file_that_cannot_be_rewritten_is_left_as_it_was);
    RUN_TEST(test_a_missing_file_is_created_by_the_first_source_saved);
    RUN_TEST(test_a_cut_save_never_loses_the_last_whole_file);
    RUN_TEST(test_a_failed_rename_keeps_the_old_file_and_leaves_nothing_else);
    RUN_TEST(test_two_saves_in_a_row_both_reach_the_file);
    RUN_TEST(test_e_opens_the_row_of_the_bar_with_its_name_in_the_field);
    RUN_TEST(test_return_saves_the_name_in_the_file_and_on_the_screen);
    RUN_TEST(test_source_2_is_added_on_an_empty_list);
    RUN_TEST(test_an_empty_row_below_the_last_takes_the_first_free_number);
    RUN_TEST(test_an_invalid_name_stays_in_the_field_and_changes_nothing);
    RUN_TEST(test_esc_and_an_empty_text_on_an_empty_row_change_nothing);
    RUN_TEST(test_writing_no_name_removes_that_source_line_only);
    RUN_TEST(test_an_empty_text_deletes_the_source_and_the_next_ones_move_up);
    RUN_TEST(test_a_delete_that_cannot_be_written_keeps_the_source);
    RUN_TEST(test_a_save_repeated_by_siov_deletes_one_source);
    RUN_TEST(test_e_on_source_1_or_without_a_file_says_why);
    RUN_TEST(test_a_file_that_cannot_be_written_says_so_and_keeps_the_source);
    RUN_TEST(test_e_creates_the_sources_file_when_it_is_missing);
    RUN_TEST(test_a_sources_file_that_cannot_be_created_says_so_and_keeps_the_sources);
    RUN_TEST(test_text_command_gets_ack_and_complete_and_a_retry_writes_the_same);
    return UNITY_END();
}
