#pragma once

// The test rig of the menu over TNFS (menu-tnfs T012), shared by test_menu_tnfs
// and test_menu_navegacion (menu-navegacion N003). Each test program includes it
// once: it replaces operator new to count the menu thread's allocations.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "fakes/fake_clock.hpp"
#include "fakes/fake_tnfs_server.hpp"
#include "fakes/gated_port.hpp"
#include "host_image_repository.hpp"
#include "memory_image_store.hpp"
#include "menu_catalog.hpp"
#include "menu_controller.hpp"
#include "menu_image.hpp"
#include "menu_sources.hpp"
#include "rb/app/drive_bay.hpp"
#include "rb/app/inline_drive_mailbox.hpp"
#include "rb/app/usecases/apply_drive_request.hpp"
#include "rb/app/usecases/unmount_disk_image.hpp"
#include "rb/net_tnfs/tnfs_codec.hpp"
#include "support/atr_header.hpp"
#include "tnfs_loader.hpp"
#include "unity.h"

#ifndef RB_TEST_MENU_DIR
#error "RB_TEST_MENU_DIR must hold the images of make_atr.py --demo"
#endif

// --- Allocations on the menu thread ----------------------------------------------

namespace {
thread_local bool t_counting = false;
thread_local unsigned long t_allocations = 0;

void* counted(std::size_t size) noexcept
{
    if (t_counting) {
        ++t_allocations;
    }
    return std::malloc(size == 0 ? 1 : size);
}
}  // namespace

// Built with -fno-exceptions: allocation failure aborts instead of throwing.
void* operator new(std::size_t size)
{
    void* p = counted(size);
    if (p == nullptr) {
        std::abort();
    }
    return p;
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return counted(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return counted(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace tnfs = rb::net_tnfs;
using rb::core::ErrorCode;
using rb::core::Span;
using rb::domain::DriveNumber;
using rb::host::MemoryImageStore;
using rb::host::MenuReply;
using rb::host::TnfsLink;
using rb::host::TnfsLoader;
using rb::test::FakeClock;
using rb::test::FakeTnfsServer;
using rb::test::GatedPort;
using Bytes = std::vector<uint8_t>;

namespace {

// Atari keyboard codes.
constexpr uint8_t K1 = 0x1F, K2 = 0x1E, K3 = 0x1A, K4 = 0x18, K5 = 0x1D;
constexpr uint8_t KA = 0x3F, KB = 0x15, KC = 0x12, KE = 0x2A, KS = 0x3E, KU = 0x0B, KESC = 0x1C;
constexpr uint8_t kPoll = rb::host::kMenuPollKey;
constexpr uint8_t kRedraw = rb::host::kMenuNoKey;

constexpr const char* kLogoPath = "/Aportes_Juan_Larrain/0- Prueba/Atari Logo.atr";

FakeClock fakeClock;  // read by the loader thread only, while a loader runs

struct Clock final : tnfs::TnfsClock {
    uint64_t nowMicros() const override { return fakeClock.nowMicros(); }
    void sleepUntil(uint64_t micros) override { fakeClock.waitUntil(micros); }
};

struct FakeLink final : TnfsLink {
    explicit FakeLink(rb::netsio::DatagramPort& p) : port_(p) {}
    rb::core::Result<void> open() override
    {
        if (failWith) {
            return rb::core::Failure{*failWith};
        }
        return {};
    }
    rb::netsio::DatagramPort& port() override { return port_; }
    rb::netsio::DatagramPort& port_;
    std::optional<ErrorCode> failWith;
};

struct TestMemory final : rb::host::ImageMemory {
    bool allocate(rb::host::ImageBuffer& out, uint32_t size) override
    {
        if (fail) {
            return false;
        }
        out = rb::host::ImageBuffer::allocate(size);
        return out.valid();
    }
    std::atomic<bool> fail{false};
};

struct Events final : rb::host::MenuEvents {
    // The recorder's own strings are the test's, not the menu's: not counted.
    void menuEvent(const char* name, const char* detail) override
    {
        const bool counting = t_counting;
        t_counting = false;
        log.push_back(std::string(name) + " " + detail);
        t_counting = counting;
    }
    bool has(const char* text) const
    {
        for (const std::string& line : log) {
            if (line.find(text) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
    std::vector<std::string> log;
};

std::array<uint8_t, 40> line(const char* text)
{
    std::array<uint8_t, 40> out{};
    rb::host::screenLine(text, Span<uint8_t>{out});
    return out;
}

void assert_line(const MenuReply& reply, unsigned offset, const char* text)
{
    const std::array<uint8_t, 40> expected = line(text);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), reply.screen() + offset, 40);
}

// A list row in inverse video, edge to edge: the cursor (menu-navegacion).
void assert_marked(const MenuReply& reply, unsigned offset, const char* text)
{
    std::array<uint8_t, 40> expected = line(text);
    for (uint8_t& b : expected) {
        b = static_cast<uint8_t>(b | 0x80);
    }
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), reply.screen() + offset, 40);
}

// The source and folder of a list, on the path line (N008); the title above it never changes.
void assert_title(const MenuReply& reply, const char* text)
{
    const std::array<uint8_t, 40> title = line(" RETROBRIDGE CHILE");
    TEST_ASSERT_EQUAL_UINT8_ARRAY(title.data(), reply.screen() + rb::host::kListTitle, rb::host::kMenuTitleWidth);
    const std::array<uint8_t, 40> expected = line(text);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), reply.screen() + rb::host::kListPath, 40);
}

[[maybe_unused]] unsigned driveLine(unsigned n) { return rb::host::kMainDrives + (n - 1) * 40; }
unsigned row(unsigned n) { return rb::host::kListEntries + n * 40; }
constexpr unsigned kMainMessage = rb::host::kMainMessage;
constexpr unsigned kListMessage = rb::host::kListMessage;

// A valid single-density ATR whose sectors carry a pattern.
Bytes atr(uint32_t sectors, uint8_t seed)
{
    const uint32_t data = rb::test::sd_data_size(sectors);
    const auto header = rb::test::make_atr_header(data, 128);
    Bytes bytes(header.begin(), header.end());
    for (uint32_t i = 0; i < data; ++i) {
        bytes.push_back(static_cast<uint8_t>(i * 7 + seed));
    }
    return bytes;
}

std::size_t slotsInUse(const MemoryImageStore& store)
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < MemoryImageStore::kSlots; ++i) {
        count += store.inUse(i) ? 1 : 0;
    }
    return count;
}

// Frees happen on the loader thread: wait for them in real time.
[[maybe_unused]] void awaitSlots(const MemoryImageStore& store, std::size_t count)
{
    for (int i = 0; i < 5000 && slotsInUse(store) != count; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    TEST_ASSERT_EQUAL_size_t(count, slotsInUse(store));
}

bool readSources(rb::host::MenuSources& sources, const char* text)
{
    std::FILE* file = std::tmpfile();
    TEST_ASSERT_NOT_NULL(file);
    std::fputs(text, file);
    std::rewind(file);
    char error[96];
    const bool ok = sources.read(file, error, sizeof error);
    std::fclose(file);
    return ok;
}

// main.cpp's wiring with TNFS sources: source 2 is the fake server, source 3 a
// name that does not resolve, source 4 a name without a link.
struct Rig {
    rb::host::MenuCatalog catalog;
    rb::host::MenuSources sources;
    rb::host::MenuImage menu;
    std::optional<rb::host::HostImageRepository> repository;
    rb::app::DriveBay bay{8};
    rb::app::InlineDriveMailbox mailbox;
    rb::app::ApplyDriveRequest apply{bay, mailbox};
    FakeTnfsServer server{fakeClock};
    GatedPort gated{server};
    FakeLink link{gated};
    FakeLink unresolved{gated};
    TnfsLink* links[TnfsLoader::kMaxSource + 1] = {};
    Clock clock;
    TestMemory memory;
    Events events;
    std::optional<TnfsLoader> loader;
    std::optional<rb::host::MenuController> controller;

    Rig()
    {
        TEST_ASSERT_TRUE(rb::host::readCatalog(RB_TEST_MENU_DIR, catalog));
        TEST_ASSERT_TRUE(readSources(sources, "pulgarcito\nno-such-host\nsin-enlace\n"));
        repository.emplace(RB_TEST_MENU_DIR, menu);
        unresolved.failWith = ErrorCode::NotFound;
        links[2] = &link;
        links[3] = &unresolved;
        tree();
    }

    // The loader starts once the test has set up the server (it is not thread-safe).
    void start()
    {
        loader.emplace(links, repository->memory(), clock, memory);
        controller.emplace(catalog, sources, *repository, mailbox, &events, &*loader);
        controller->onColdReset();
        controller->service();
        step();
    }

    void tree()
    {
        server.addDirectory("/Juegos");
        server.addDirectory("/Vacia");
        server.addFile("/b.atr", atr(720, 1));
        server.addFile("/A.ATR", atr(720, 2));
        server.addFile("/notas.txt", Bytes(10, 1));
        server.addFile("/.oculto.atr", atr(720, 3));
        server.addFile("/Juegos/rota.atr", Bytes{'n', 'o', ' ', 'a', 't', 'r'});
        server.addFile(kLogoPath, atr(720, 9));
        server.addFile("/Aportes_Juan_Larrain/0- Prueba/leeme.txt", Bytes(5, 2));
    }

    void step()
    {
        apply.execute();
        controller->service();
    }

    rb::host::MenuReply shown;  // the screen the Atari shows (with the moves done there)
    unsigned hostCalls = 0;      // 'K' round trips, the moves done on the Atari not counted

    // What the 6502 does with a code before it sends it (tools/make_menu.py, N007): on a
    // shown screen with a local cursor, up and down move the bar there; RETURN and fire send
    // kMenuConfirmRow + row; at an edge, and for left and right, the host is asked only when
    // there is a page that way. false: done on the Atari, nothing sent.
    bool atari(uint8_t& code)
    {
        uint8_t* block = shown.bytes.data() + 2 + rb::host::kMenuCursor;
        const unsigned rows = block[2];
        if (shown.action() != rb::host::kMenuActionShow || rows == 0) {
            return true;
        }
        const unsigned base = block[0] | (block[1] << 8);
        uint8_t& row = block[3];
        const auto flip = [&] {
            uint8_t* line = shown.bytes.data() + 2 + base + row * 40u;
            for (unsigned i = 0; i < 40; ++i) {
                line[i] = static_cast<uint8_t>(line[i] ^ 0x80);
            }
        };
        bool edge = false;
        switch (code) {
        case rb::host::kMenuNavUp:
            edge = row == 0;
            if (!edge) {
                flip();
                --row;
                flip();
            }
            break;
        case rb::host::kMenuNavDown:
            edge = row + 1u >= rows;
            if (!edge) {
                flip();
                ++row;
                flip();
            }
            break;
        case rb::host::kMenuNavLeft:
        case rb::host::kMenuNavRight:
            edge = true;
            break;
        case rb::host::kMenuNavFire:
            code = static_cast<uint8_t>(rb::host::kMenuConfirmRow + row);
            return true;
        default:
            if (code != rb::host::kMenuNoKey && code != rb::host::kMenuPollKey && (code & 0x3F) == 0x0C) {
                code = static_cast<uint8_t>(rb::host::kMenuConfirmRow + row);  // RETURN, also with Shift
            }
            return true;
        }
        // Only toward a page that exists: up and left need one before, down and right one after.
        const bool before = code == rb::host::kMenuNavUp || code == rb::host::kMenuNavLeft;
        return edge && (block[4] & (before ? rb::host::kMenuPageBefore : rb::host::kMenuPageAfter)) != 0;
    }

    // One code from the Atari: done there when the 6502 does it by itself (shown, no host
    // call), else one 'K' round trip (rawPress).
    MenuReply press(uint8_t code)
    {
        if (!atari(code)) {
            return shown;
        }
        return rawPress(code);
    }

    // One 'K' round trip: timed, and without any allocation on the menu thread
    // (ADR-011), whether it starts, polls, ends or cancels a job.
    MenuReply rawPress(uint8_t code)
    {
        ++hostCalls;
        const auto begin = std::chrono::steady_clock::now();
        t_allocations = 0;
        t_counting = true;
        const MenuReply reply = controller->key(code);
        t_counting = false;
        const auto took = std::chrono::steady_clock::now() - begin;
        TEST_ASSERT_TRUE_MESSAGE(took <= std::chrono::milliseconds(250), "a 'K' took more than 250 ms");
        TEST_ASSERT_TRUE(reply.action() <= rb::host::kMenuActionWait);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(0, t_allocations, "key() allocated on the menu thread");
        step();
        shown = reply;
        return reply;
    }

    // Presses `code`, then polls as the 6502 does until the load ends.
    MenuReply settle(uint8_t code)
    {
        MenuReply reply = press(code);
        for (int i = 0; i < 200 && reply.action() == rb::host::kMenuActionWait; ++i) {
            reply = press(kPoll);
        }
        TEST_ASSERT_NOT_EQUAL(rb::host::kMenuActionWait, reply.action());
        return reply;
    }
    MenuReply walk(std::initializer_list<uint8_t> codes)
    {
        MenuReply last;
        for (uint8_t code : codes) {
            last = settle(code);
        }
        return last;
    }

    const char* mounted(unsigned n) const
    {
        const auto* location = bay.mountedLocation(DriveNumber::from(static_cast<uint8_t>(n)).value());
        return location != nullptr ? location->c_str() : "";
    }
    const MemoryImageStore& store() { return repository->memory(); }
};

// The pattern of the screen while a load is in progress.
void assert_loading(const MenuReply& reply, unsigned offset, const char* percent)
{
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionWait, reply.action());
    char text[24];
    std::snprintf(text, sizeof text, " CARGANDO %s%%", percent);
    assert_line(reply, offset, text);
}

[[maybe_unused]] unsigned percentOf(const MenuReply& reply, unsigned offset)
{
    // " CARGANDO nn%": the digits start at column 10.
    unsigned value = 0;
    const std::array<uint8_t, 40> digits = line("0123456789");
    for (unsigned i = 10; i < 13; ++i) {
        const uint8_t c = reply.screen()[offset + i];
        bool found = false;
        for (unsigned d = 0; d < 10; ++d) {
            if (digits[d] == c) {
                value = value * 10 + d;
                found = true;
            }
        }
        if (!found) {
            break;
        }
    }
    return value;
}

}  // namespace
