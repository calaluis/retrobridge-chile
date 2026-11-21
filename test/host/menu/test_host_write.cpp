// Disk writes in retrobridge_host with --images (menu-escritura-disco, W005): the
// --images wiring of main.cpp minus NetSIO (catalog, HostImageRepository with
// writable files, controller, MenuSioTransport with a sio_write observer,
// SioCommandDispatcher with allowWrites, SioSession) over a ScriptedSioTransport.
// Each test works on fresh copies of the demo images (fixture menu_images).

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "rb/menu/create_atr.hpp"
#include "fakes/fake_clock.hpp"
#include "fakes/memory_byte_source.hpp"
#include "fakes/scripted_sio_transport.hpp"
#include "rb/menu/host_image_repository.hpp"
#include "rb/menu/memory_image_store.hpp"
#include "rb/menu/menu_catalog.hpp"
#include "rb/menu/menu_controller.hpp"
#include "rb/menu/menu_image.hpp"
#include "rb/menu/menu_sio_transport.hpp"
#include "rb/menu/menu_sources.hpp"
#include "rb/app/drive_bay.hpp"
#include "rb/app/image_location.hpp"
#include "rb/app/inline_drive_mailbox.hpp"
#include "rb/app/usecases/apply_drive_request.hpp"
#include "rb/domain/disk_image.hpp"
#include "rb/sio/sio_checksum.hpp"
#include "rb/sio/sio_command_dispatcher.hpp"
#include "rb/sio/sio_session.hpp"
#include "rb/menu/sio_write_log.hpp"
#include "support/atr_header.hpp"
#include "unity.h"

#ifndef RB_TEST_MENU_DIR
#error "RB_TEST_MENU_DIR must hold the images of make_atr.py --demo"
#endif
#ifndef RB_TEST_WRITE_DIR
#error "RB_TEST_WRITE_DIR must name a scratch folder for the copies"
#endif

using rb::core::Span;
using rb::domain::DriveNumber;
using rb::host::MenuReply;
using rb::sio::StepOutcome;

namespace {

constexpr uint8_t K1 = 0x1F, K2 = 0x1E, K3 = 0x1A, K4 = 0x18, K5 = 0x1D, KB = 0x15;
constexpr uint8_t kWrite = 0x57, kPut = 0x50, kRead = 0x52, kStatus = 0x53;
constexpr uint8_t kFormat = 0x21, kFormatMedium = 0x22;
constexpr uint8_t KN = 0x23, KS = 0x3E, KM = 0x25, KEsc = 0x1C;
// Files the create tests make; setUp removes them.
const char* const kCreated[] = {"NUEVO.ATR", "MEDIA.ATR", "DEMO-T.ATR", "AAA.ATR", "DISCO-1.ATR"};

std::string path_of(const char* folder, const char* name)
{
    return std::string(folder) + "/" + name;
}

int set_writable(const std::string& path, bool writable)
{
#ifdef _WIN32
    return _chmod(path.c_str(), writable ? (_S_IREAD | _S_IWRITE) : _S_IREAD);
#else
    return chmod(path.c_str(), writable ? 0644 : 0444);
#endif
}

std::vector<uint8_t> file_bytes(const std::string& path)
{
    std::vector<uint8_t> bytes;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    TEST_ASSERT_NOT_NULL(file);
    int c = 0;
    while ((c = std::fgetc(file)) != EOF) {
        bytes.push_back(static_cast<uint8_t>(c));
    }
    std::fclose(file);
    return bytes;
}

void write_file(const std::string& path, const std::vector<uint8_t>& bytes)
{
    std::FILE* file = std::fopen(path.c_str(), "wb");
    TEST_ASSERT_NOT_NULL_MESSAGE(file, path.c_str());
    TEST_ASSERT_EQUAL_size_t(bytes.size(), std::fwrite(bytes.data(), 1, bytes.size(), file));
    std::fclose(file);
}

// Fresh, writable copies of the demo images in RB_TEST_WRITE_DIR.
void fresh_copies()
{
#ifdef _WIN32
    (void)_mkdir(RB_TEST_WRITE_DIR);
#else
    (void)mkdir(RB_TEST_WRITE_DIR, 0755);
#endif
    for (const char* name : {"demo-dos.atr", "demo-uno.atr"}) {
        const std::string copy = path_of(RB_TEST_WRITE_DIR, name);
        (void)set_writable(copy, true);  // a previous run may have left it read-only
        write_file(copy, file_bytes(path_of(RB_TEST_MENU_DIR, name)));
    }
    for (const char* name : kCreated) {
        (void)std::remove(path_of(RB_TEST_WRITE_DIR, name).c_str());
    }
}

bool file_exists(const std::string& path)
{
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file != nullptr) {
        std::fclose(file);
    }
    return file != nullptr;
}

uint16_t header_sector_size(const std::vector<uint8_t>& atr)
{
    return static_cast<uint16_t>(atr[4] | (atr[5] << 8));
}

std::array<uint8_t, 5> frame(uint8_t device, uint8_t command, uint16_t aux = 0)
{
    std::array<uint8_t, 5> bytes{device, command, static_cast<uint8_t>(aux & 0xFF), static_cast<uint8_t>(aux >> 8), 0};
    bytes[4] = rb::sio::sio_checksum(Span<const uint8_t>{bytes.data(), 4});
    return bytes;
}

std::array<uint8_t, 257> data_frame(std::size_t length, uint8_t seed, bool good = true)
{
    std::array<uint8_t, 257> bytes{};
    for (std::size_t i = 0; i < length; ++i) {
        bytes[i] = static_cast<uint8_t>(seed + i * 3u);
    }
    bytes[length] =
        static_cast<uint8_t>(rb::sio::sio_checksum(Span<const uint8_t>{bytes.data(), length}) ^ (good ? 0 : 1));
    return bytes;
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

class RecordingWrites final : public rb::host::SioWriteObserver {
public:
    explicit RecordingWrites(rb::app::DriveBay& bay) : bay_(bay) {}
    void onSioWrite(const rb::host::SioWriteRecord& record) override
    {
        ++count;
        last = record;
        const auto number = DriveNumber::from(record.drive);
        rb::host::formatSioWrite(record, number.ok() ? bay_.find(number.value()) : nullptr, detail,
                                 sizeof detail);
    }
    unsigned count = 0;
    rb::host::SioWriteRecord last{};
    char detail[96] = {};

private:
    rb::app::DriveBay& bay_;
};

class RecordingEvents final : public rb::host::MenuEvents {
public:
    void menuEvent(const char* name, const char* detail) override
    {
        if (std::strcmp(name, "menu_create") == 0) {
            ++creates;
            std::snprintf(create, sizeof create, "%s", detail);
        }
    }
    unsigned creates = 0;
    char create[128] = {};
};

struct WriteRig {
    rb::host::MenuCatalog catalog;
    RecordingEvents events;
    rb::host::MenuSources sources;
    rb::host::MenuImage menu;
    std::optional<rb::host::HostImageRepository> repository;
    rb::app::DriveBay bay{8};
    rb::app::InlineDriveMailbox mailbox;
    rb::app::ApplyDriveRequest apply{bay, mailbox};
    std::optional<rb::host::MenuController> controller;
    rb::test::FakeClock clock;
    rb::test::ScriptedSioTransport inner{clock};
    RecordingWrites writes{bay};
    std::optional<rb::host::MenuSioTransport> transport;
    rb::sio::SioCommandDispatcher dispatcher{bay, true};
    std::optional<rb::sio::SioSession> session;

    explicit WriteRig(bool images = true)
    {
        TEST_ASSERT_TRUE(rb::host::readCatalog(RB_TEST_WRITE_DIR, catalog));
        TEST_ASSERT_TRUE(readSources(sources, "tnfs.example.cl\n"));
        repository.emplace(RB_TEST_WRITE_DIR, menu);
        controller.emplace(catalog, sources, *repository, mailbox, &events);
        if (images) {
            controller->setImagesFolder(RB_TEST_WRITE_DIR, catalog);
        }
        controller->onColdReset();
        controller->service();
        step();
        transport.emplace(inner, clock, *controller, &writes);
        session.emplace(*transport, clock, dispatcher, apply);
    }

    void step()
    {
        apply.execute();
        controller->service();
    }

    MenuReply keys(std::initializer_list<uint8_t> codes)
    {
        MenuReply last;
        for (uint8_t code : codes) {
            last = controller->key(code);
            step();
        }
        return last;
    }

    // One SIO transaction through the decorator and the session.
    StepOutcome sio(uint8_t drive, uint8_t command, uint16_t sector)
    {
        inner.push_frame(frame(static_cast<uint8_t>(0x30 + drive), command, sector));
        return session->step();
    }

    // N, the name typed by the 6502 ('T' then kMenuEditSave), then the given keys.
    MenuReply create(const char* name, std::initializer_list<uint8_t> after)
    {
        keys({KN});
        for (uint8_t i = 0; name[i] != '\0'; ++i) {
            controller->text(i, static_cast<uint8_t>(name[i]));
        }
        MenuReply last = keys({static_cast<uint8_t>(rb::host::kMenuEditSave)});
        if (after.size() != 0) {
            last = keys(after);
        }
        return last;
    }

    StepOutcome write(uint8_t drive, uint8_t command, uint16_t sector, const std::array<uint8_t, 257>& data,
                      std::size_t length)
    {
        inner.push_data(data.data(), length + 1);
        return sio(drive, command, sector);
    }
};

void assert_drive_line(WriteRig& rig, unsigned n, const char* expected)
{
    std::array<uint8_t, 40> line{};
    rb::host::screenLine(expected, Span<uint8_t>{line});
    const MenuReply reply = rig.controller->key(rb::host::kMenuNoKey);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(line.data(), reply.screen() + rb::host::kMainDrives + (n - 1) * 40, 40);
}

void assert_message(WriteRig& rig, const char* expected)
{
    std::array<uint8_t, 40> line{};
    rb::host::screenLine(expected, Span<uint8_t>{line});
    const MenuReply reply = rig.controller->key(rb::host::kMenuNoKey);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(line.data(), reply.screen() + rb::host::kMainMessage, 40);
}

// Every data byte of an ATR (after the 16-byte header) is zero.
bool all_zero_after_header(const std::vector<uint8_t>& bytes)
{
    for (std::size_t i = 16; i < bytes.size(); ++i) {
        if (bytes[i] != 0) {
            return false;
        }
    }
    return bytes.size() > 16;
}

// A protected drive's line: the name cut to 29 columns, then " SL".
std::string locked(unsigned n, const char* name)
{
    char text[48];
    std::snprintf(text, sizeof text, "D%u %-29.29s SL", n, name);
    return text;
}

}  // namespace

void setUp(void)
{
    fresh_copies();
}

void tearDown(void)
{
    (void)set_writable(path_of(RB_TEST_WRITE_DIR, "demo-uno.atr"), true);
}

// AC1, AC2, AC3: P and W on a writable ATR; read back; kept after a restart.
static void test_put_and_write_are_read_back_and_survive_a_restart(void)
{
    const std::string file = path_of(RB_TEST_WRITE_DIR, "demo-uno.atr");
    const uint16_t size = header_sector_size(file_bytes(file));
    const uint16_t sector = 10;  // sector 4 onwards uses the header's size
    const auto put = data_frame(size, 0x21);
    const auto written = data_frame(size, 0x55);
    {
        WriteRig rig;
        rig.keys({K1, KB, K2});
        TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.bay.mountedLocation(DriveNumber::from(2).value())->c_str());
        assert_drive_line(rig, 2, "D2 DEMO-UNO.ATR");  // writable: no mark

        TEST_ASSERT_TRUE(rig.write(2, kPut, sector, put, size) == StepOutcome::Completed);
        TEST_ASSERT_EQUAL_HEX8(0x43, rig.inner.sent(rig.inner.sends() - 1).bytes[0]);
        const std::size_t before = rig.inner.sends();
        TEST_ASSERT_TRUE(rig.sio(2, kRead, sector) == StepOutcome::Completed);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(put.data(), rig.inner.sent(before + 1).bytes.data() + 1, size);

        TEST_ASSERT_TRUE(rig.write(2, kWrite, sector, written, size) == StepOutcome::Completed);
        TEST_ASSERT_EQUAL_UINT(2, rig.writes.count);
        TEST_ASSERT_EQUAL('C', rig.writes.last.result);
        TEST_ASSERT_EQUAL_UINT32(rb::host::crc32(written.data(), size), rig.writes.last.crc32);
    }  // the host closes: every file is closed
    const std::vector<uint8_t> bytes = file_bytes(file);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(written.data(), bytes.data() + 16 + 3u * 128u + (sector - 4u) * size, size);

    WriteRig again;
    again.keys({K1, KB, K2});
    TEST_ASSERT_TRUE(again.sio(2, kRead, sector) == StepOutcome::Completed);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(written.data(), again.inner.sent(1).bytes.data() + 1, size);
}

// AC8: a SIOV retry of the same write leaves the file as one write does.
static void test_retried_write_leaves_the_file_as_one_write(void)
{
    const std::string file = path_of(RB_TEST_WRITE_DIR, "demo-uno.atr");
    const uint16_t size = header_sector_size(file_bytes(file));
    const auto data = data_frame(size, 0x3C);
    {
        WriteRig rig;
        rig.keys({K1, KB, K2});
        TEST_ASSERT_TRUE(rig.write(2, kWrite, 10, data, size) == StepOutcome::Completed);
    }
    const std::vector<uint8_t> once = file_bytes(file);
    fresh_copies();
    {
        WriteRig rig;
        rig.keys({K1, KB, K2});
        TEST_ASSERT_TRUE(rig.write(2, kWrite, 10, data, size) == StepOutcome::Completed);
        TEST_ASSERT_TRUE(rig.write(2, kWrite, 10, data, size) == StepOutcome::Completed);
        TEST_ASSERT_EQUAL_UINT(2, rig.writes.count);
    }
    TEST_ASSERT_TRUE(once == file_bytes(file));
}

// AC1: mounting the same ATR again on the same drive keeps it writable (closing review, option A).
static void test_remounting_the_same_atr_keeps_it_writable(void)
{
    const std::string file = path_of(RB_TEST_WRITE_DIR, "demo-uno.atr");
    const uint16_t size = header_sector_size(file_bytes(file));
    const auto data = data_frame(size, 0x5A);
    WriteRig rig;
    rig.keys({K1, KB, K2});
    rig.keys({K1, KB, K2});
    rig.step();  // the remount is an unmount, then a mount
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.bay.mountedLocation(DriveNumber::from(2).value())->c_str());
    assert_drive_line(rig, 2, "D2 DEMO-UNO.ATR");  // no " SL"
    TEST_ASSERT_TRUE(rig.write(2, kWrite, 10, data, size) == StepOutcome::Completed);
    TEST_ASSERT_EQUAL('C', rig.writes.last.result);
}

// AC4, AC7: a read-only file is protected in STATUS, refuses writes, keeps its bytes.
static void test_read_only_file_is_protected_marked_and_unchanged(void)
{
    const std::string file = path_of(RB_TEST_WRITE_DIR, "demo-uno.atr");
    TEST_ASSERT_EQUAL_INT(0, set_writable(file, false));
    const std::vector<uint8_t> before = file_bytes(file);
    WriteRig rig;
    rig.keys({K1, KB, K2});
    assert_drive_line(rig, 2, locked(2, "DEMO-UNO.ATR").c_str());

    TEST_ASSERT_TRUE(rig.sio(2, kStatus, 0) == StepOutcome::Completed);
    TEST_ASSERT_EQUAL_HEX8(0xBF, rig.inner.sent(1).bytes[2]);  // [C][byte 0][byte 1]...
    const uint16_t size = header_sector_size(before);
    TEST_ASSERT_TRUE(rig.write(2, kWrite, 10, data_frame(size, 0x77), size) == StepOutcome::Completed);
    TEST_ASSERT_EQUAL_HEX8(0x45, rig.inner.sent(rig.inner.sends() - 1).bytes[0]);  // ERROR
    TEST_ASSERT_EQUAL_STRING_LEN("drive=2 cmd=W sector=10 result=error crc32=", rig.writes.detail, 43);
    TEST_ASSERT_NOT_NULL(std::strstr(rig.writes.detail, " cause=protected"));
    TEST_ASSERT_TRUE(before == file_bytes(file));
}

// The same ATR on two drives: only the first one writes (spec assumption).
static void test_same_atr_on_two_drives_only_the_first_writes(void)
{
    WriteRig rig;
    rig.keys({K1, KB, K2});
    rig.keys({K1, KB, K3});
    assert_drive_line(rig, 2, "D2 DEMO-UNO.ATR");
    assert_drive_line(rig, 3, locked(3, "DEMO-UNO.ATR").c_str());
    const uint16_t size = header_sector_size(file_bytes(path_of(RB_TEST_WRITE_DIR, "demo-uno.atr")));
    TEST_ASSERT_TRUE(rig.write(3, kPut, 10, data_frame(size, 1), size) == StepOutcome::Completed);
    TEST_ASSERT_EQUAL('E', rig.writes.last.result);
}

// AC6: the menu on D1 refuses writes and does not change.
static void test_menu_on_d1_refuses_writes(void)
{
    WriteRig rig;
    TEST_ASSERT_EQUAL_STRING(rb::host::kMenuLocation,
                             rig.bay.mountedLocation(DriveNumber::from(1).value())->c_str());
    std::vector<uint8_t> before(rig.menu.size());
    TEST_ASSERT_TRUE(rig.menu.readAt(0, Span<uint8_t>{before.data(), before.size()}).ok());
    TEST_ASSERT_TRUE(rig.write(1, kWrite, 4, data_frame(128, 9), 128) == StepOutcome::Completed);
    TEST_ASSERT_EQUAL('E', rig.writes.last.result);
    TEST_ASSERT_NOT_NULL(std::strstr(rig.writes.detail, " cause=protected"));
    std::vector<uint8_t> after(rig.menu.size());
    TEST_ASSERT_TRUE(rig.menu.readAt(0, Span<uint8_t>{after.data(), after.size()}).ok());
    TEST_ASSERT_TRUE(before == after);
}

// AC5: an image brought over TNFS is never writable.
static void test_tnfs_image_is_protected(void)
{
    WriteRig rig;
    const std::vector<uint8_t> atr = file_bytes(path_of(RB_TEST_WRITE_DIR, "demo-dos.atr"));
    const auto slot =
        rig.repository->memory().adopt(rb::host::ImageBuffer::copyOf(Span<const uint8_t>{atr.data(), atr.size()}));
    TEST_ASSERT_TRUE(slot.ok());
    char location[8] = "*tnfs*0";
    location[6] = static_cast<char>('0' + slot.value());
    const auto opened = rig.repository->open(rb::app::ImageLocation::from(location).value());
    TEST_ASSERT_TRUE(opened.ok());
    TEST_ASSERT_FALSE(opened.value()->writable());
    {
        const auto disk = rb::domain::DiskImage::open(*opened.value());
        TEST_ASSERT_TRUE(disk.ok());
        TEST_ASSERT_TRUE(disk.value().readOnly());
    }
    rig.repository->close(opened.value());
}

// AC9: a write the PC cannot save answers ERROR and the log names the cause.
static void test_failed_save_answers_error_with_cause_io(void)
{
    WriteRig rig;
    rb::test::MemoryByteSource source{rb::test::make_atr_header(rb::test::sd_data_size(720), 128),
                                      rb::test::kAtrHeaderSize + rb::test::sd_data_size(720), true};
    source.fail_writes(true);
    TEST_ASSERT_TRUE(rig.bay
                         .mount(DriveNumber::from(4).value(), rb::domain::DiskImage::open(source).value(),
                                rb::app::ImageLocation::from("full.atr").value())
                         .ok());
    TEST_ASSERT_TRUE(rig.write(4, kWrite, 10, data_frame(128, 3), 128) == StepOutcome::Completed);
    TEST_ASSERT_EQUAL_HEX8(0x45, rig.inner.sent(rig.inner.sends() - 1).bytes[0]);
    TEST_ASSERT_NOT_NULL(std::strstr(rig.writes.detail, " cause=io"));
    (void)rig.bay.unmount(DriveNumber::from(4).value());
}

// The sio_write line: NAK of the command (no data) and of a bad data frame.
static void test_naks_are_logged(void)
{
    WriteRig rig;
    rig.keys({K1, KB, K2});
    TEST_ASSERT_TRUE(rig.sio(2, kWrite, 0) == StepOutcome::Nacked);  // sector 0
    TEST_ASSERT_EQUAL_STRING("drive=2 cmd=W sector=0 result=nak", rig.writes.detail);
    TEST_ASSERT_TRUE(rig.write(2, kPut, 10, data_frame(128, 4, false), 128) != StepOutcome::Completed);
    TEST_ASSERT_EQUAL('N', rig.writes.last.result);
    TEST_ASSERT_EQUAL_STRING_LEN("drive=2 cmd=P sector=10 result=nak crc32=", rig.writes.detail, 41);
    TEST_ASSERT_NULL(std::strstr(rig.writes.detail, "cause="));
    TEST_ASSERT_EQUAL_UINT(2, rig.writes.count);
}

// A write whose COMPLETE cannot be sent is still logged: the file changed.
static void test_lost_completion_is_still_logged(void)
{
    WriteRig rig;
    rig.keys({K1, KB, K2});
    const std::size_t sends = rig.inner.sends();
    rig.inner.fail_send(sends + 2);  // command ACK, data ACK, then COMPLETE fails
    const uint16_t size = header_sector_size(file_bytes(path_of(RB_TEST_WRITE_DIR, "demo-uno.atr")));
    TEST_ASSERT_TRUE(rig.write(2, kWrite, 10, data_frame(size, 0x31), size) == StepOutcome::TransportFailed);
    TEST_ASSERT_EQUAL_UINT(1, rig.writes.count);
    TEST_ASSERT_EQUAL('C', rig.writes.last.result);
    TEST_ASSERT_NOT_NULL(std::strstr(rig.writes.detail, "result=complete crc32="));
    TEST_ASSERT_NOT_NULL(std::strstr(rig.writes.detail, " reply=lost"));
}

// ---------------------------------------------------------------------------
// menu-formatear-disco F004: N creates an ATR; $21/$22 format it; sio_write logs F/FM.

// AC5: N, a name, S and 2: NUEVO.ATR (720 x 128, zeros) is created, listed and mounted on D2.
static void test_create_mounts_a_new_single_density_atr(void)
{
    WriteRig rig;
    const MenuReply naming = rig.keys({KN});
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionEdit, naming.action());
    const unsigned field = rb::host::kMainMessage + 7;  // after "NOMBRE:"
    TEST_ASSERT_EQUAL_UINT8(field & 0xFF, naming.screen()[rb::host::kMenuEdit]);
    TEST_ASSERT_EQUAL_UINT8(field >> 8, naming.screen()[rb::host::kMenuEdit + 1]);
    TEST_ASSERT_EQUAL_UINT8(0, naming.screen()[rb::host::kMenuEdit + 2]);
    rig.keys({KEsc});
    TEST_ASSERT_EQUAL_UINT(0, rig.events.creates);

    rig.create("NUEVO", {});
    assert_message(rig, " S SIMPLE  M MEDIA  ESC CANCELA");
    rig.keys({KS});
    assert_message(rig, " UNIDAD 2-8, ESC CANCELA");
    rig.keys({K2});
    const std::vector<uint8_t> bytes = file_bytes(path_of(RB_TEST_WRITE_DIR, "NUEVO.ATR"));
    TEST_ASSERT_EQUAL_size_t(16u + 720u * 128u, bytes.size());
    TEST_ASSERT_TRUE(all_zero_after_header(bytes));
    TEST_ASSERT_EQUAL_STRING("NUEVO.ATR", rig.bay.mountedLocation(DriveNumber::from(2).value())->c_str());
    assert_drive_line(rig, 2, "D2 NUEVO.ATR");  // writable
    TEST_ASSERT_EQUAL_size_t(3, rig.catalog.count());
    TEST_ASSERT_EQUAL_STRING("name=NUEVO.ATR density=single drive=2 result=created", rig.events.create);
}

// AC1, AC2: $21 on a single density ATR and $22 on a medium one write zeros; F and FM are logged.
static void test_format_single_and_medium_write_zeros_and_are_logged(void)
{
    WriteRig rig;
    rig.create("NUEVO", {KS, K2});
    rig.create("MEDIA", {KM, K3});
    TEST_ASSERT_EQUAL_STRING("name=MEDIA.ATR density=medium drive=3 result=created", rig.events.create);
    TEST_ASSERT_EQUAL_size_t(16u + 1040u * 128u, file_bytes(path_of(RB_TEST_WRITE_DIR, "MEDIA.ATR")).size());

    TEST_ASSERT_TRUE(rig.write(2, kWrite, 10, data_frame(128, 0x41), 128) == StepOutcome::Completed);
    TEST_ASSERT_FALSE(all_zero_after_header(file_bytes(path_of(RB_TEST_WRITE_DIR, "NUEVO.ATR"))));
    TEST_ASSERT_TRUE(rig.sio(2, kFormat, 0) == StepOutcome::Completed);
    TEST_ASSERT_EQUAL_HEX8(0x43, rig.inner.sent(rig.inner.sends() - 1).bytes[0]);  // COMPLETE + frame
    TEST_ASSERT_EQUAL_STRING("drive=2 cmd=F result=complete", rig.writes.detail);
    TEST_ASSERT_TRUE(all_zero_after_header(file_bytes(path_of(RB_TEST_WRITE_DIR, "NUEVO.ATR"))));

    TEST_ASSERT_TRUE(rig.write(3, kPut, 900, data_frame(128, 0x42), 128) == StepOutcome::Completed);
    TEST_ASSERT_TRUE(rig.sio(3, kFormatMedium, 0) == StepOutcome::Completed);
    TEST_ASSERT_EQUAL_STRING("drive=3 cmd=FM result=complete", rig.writes.detail);
    TEST_ASSERT_TRUE(all_zero_after_header(file_bytes(path_of(RB_TEST_WRITE_DIR, "MEDIA.ATR"))));
}

// AC3: $22 on a 720-sector ATR answers ERROR, logs cause=density and leaves the file alone.
static void test_format_with_the_other_density_is_an_error(void)
{
    WriteRig rig;
    rig.create("NUEVO", {KS, K2});
    TEST_ASSERT_TRUE(rig.write(2, kWrite, 10, data_frame(128, 0x43), 128) == StepOutcome::Completed);
    const std::vector<uint8_t> before = file_bytes(path_of(RB_TEST_WRITE_DIR, "NUEVO.ATR"));
    TEST_ASSERT_TRUE(rig.sio(2, kFormatMedium, 0) == StepOutcome::Completed);
    TEST_ASSERT_EQUAL_HEX8(0x45, rig.inner.sent(rig.inner.sends() - 1).bytes[0]);  // ERROR + frame
    TEST_ASSERT_EQUAL_STRING("drive=2 cmd=FM result=error cause=density", rig.writes.detail);
    TEST_ASSERT_TRUE(before == file_bytes(path_of(RB_TEST_WRITE_DIR, "NUEVO.ATR")));
}

// AC4: the menu on D1 and a read-only file refuse to format; an empty drive answers ERROR.
static void test_format_refused_on_protected_and_empty_drives(void)
{
    const std::string file = path_of(RB_TEST_WRITE_DIR, "demo-uno.atr");
    TEST_ASSERT_EQUAL_INT(0, set_writable(file, false));
    const std::vector<uint8_t> before = file_bytes(file);
    WriteRig rig;
    TEST_ASSERT_TRUE(rig.sio(1, kFormat, 0) == StepOutcome::Completed);
    TEST_ASSERT_EQUAL_STRING("drive=1 cmd=F result=error cause=protected", rig.writes.detail);
    rig.keys({K1, KB, K2});
    TEST_ASSERT_TRUE(rig.sio(2, kFormat, 0) == StepOutcome::Completed);
    TEST_ASSERT_EQUAL_STRING("drive=2 cmd=F result=error cause=protected", rig.writes.detail);
    TEST_ASSERT_TRUE(before == file_bytes(file));
    TEST_ASSERT_TRUE(rig.sio(5, kFormat, 0) == StepOutcome::Completed);
    TEST_ASSERT_EQUAL_STRING("drive=5 cmd=F result=error cause=no_disk", rig.writes.detail);
}

// AC6: a name already there (listed, or a file made after the list was read) changes nothing.
static void test_create_refuses_an_existing_name(void)
{
    write_file(path_of(RB_TEST_WRITE_DIR, "NUEVO.ATR"), {1, 2, 3});
    WriteRig rig;  // NUEVO.ATR is listed
    rig.create("NUEVO", {});
    assert_message(rig, " YA EXISTE NUEVO.ATR");
    TEST_ASSERT_EQUAL_STRING("name=NUEVO.ATR density=- drive=0 result=exists", rig.events.create);

    write_file(path_of(RB_TEST_WRITE_DIR, "MEDIA.ATR"), {4, 5});  // not listed
    rig.create("MEDIA", {KM, K2});
    assert_message(rig, " YA EXISTE MEDIA.ATR");
    TEST_ASSERT_EQUAL_STRING("name=MEDIA.ATR density=medium drive=2 result=exists", rig.events.create);
    TEST_ASSERT_TRUE(std::vector<uint8_t>({1, 2, 3}) == file_bytes(path_of(RB_TEST_WRITE_DIR, "NUEVO.ATR")));
    TEST_ASSERT_TRUE(std::vector<uint8_t>({4, 5}) == file_bytes(path_of(RB_TEST_WRITE_DIR, "MEDIA.ATR")));
    TEST_ASSERT_NULL(rig.bay.mountedLocation(DriveNumber::from(2).value()));
}

// AC7: an empty name, 26 characters, a space, a dot or a device name create nothing.
static void test_create_refuses_an_invalid_name(void)
{
    WriteRig rig;
    for (const char* name : {"", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "NUE VO", "A.B", "nul", "N.ATR"}) {
        rig.create(name, {});
        assert_message(rig, " NOMBRE NO VALIDO: LETRAS, 0-9 Y -");
        TEST_ASSERT_NOT_NULL(std::strstr(rig.events.create, "result=invalid_name"));
    }
    TEST_ASSERT_EQUAL_UINT(6, rig.events.creates);
    TEST_ASSERT_EQUAL_size_t(2, rig.catalog.count());
    TEST_ASSERT_FALSE(file_exists(path_of(RB_TEST_WRITE_DIR, "NUE VO.ATR")));
}

// M46-3: typed without Shift, the 6502 sends lower case; the file is named in capitals.
static void test_create_takes_lower_case_as_capitals(void)
{
    WriteRig rig;
    rig.create("disco-1", {KS, K2});
    TEST_ASSERT_EQUAL_STRING("name=DISCO-1.ATR density=single drive=2 result=created", rig.events.create);
    TEST_ASSERT_TRUE(file_exists(path_of(RB_TEST_WRITE_DIR, "DISCO-1.ATR")));
    assert_drive_line(rig, 2, "D2 DISCO-1.ATR");
}

// Esc at each step cancels; D1 is not offered (it serves the menu); without a folder N says so.
static void test_create_cancels_and_refuses_d1(void)
{
    WriteRig rig;
    rig.create("NUEVO", {KEsc});
    rig.create("NUEVO", {KS, KEsc});
    TEST_ASSERT_FALSE(file_exists(path_of(RB_TEST_WRITE_DIR, "NUEVO.ATR")));
    rig.create("NUEVO", {KS, K1});
    assert_message(rig, " UNIDAD 2-8, ESC CANCELA");
    TEST_ASSERT_FALSE(file_exists(path_of(RB_TEST_WRITE_DIR, "NUEVO.ATR")));
    rig.keys({KEsc});
    TEST_ASSERT_EQUAL_UINT(0, rig.events.creates);

    WriteRig bare{false};
    bare.keys({KN});
    assert_message(bare, " SIN CARPETA LOCAL");
    TEST_ASSERT_EQUAL_STRING("name=- density=- drive=0 result=no_folder", bare.events.create);
}

// AC8: with 999 images listed, N refuses before asking for a name.
static void test_create_refuses_a_full_list(void)
{
    rb::host::MenuCatalog catalog;
    char name[32];
    for (int i = 0; i < 999; ++i) {
        std::snprintf(name, sizeof name, "G%04d.ATR", i);
        TEST_ASSERT_TRUE(catalog.offer(name));
    }
    rb::host::MenuSources sources;
    rb::host::MenuImage menu;
    rb::host::HostImageRepository repository{RB_TEST_WRITE_DIR, menu};
    rb::app::InlineDriveMailbox mailbox;
    RecordingEvents events;
    rb::host::MenuController controller{catalog, sources, repository, mailbox, &events};
    controller.setImagesFolder(RB_TEST_WRITE_DIR, catalog);
    const MenuReply reply = controller.key(KN);
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionShow, reply.action());
    std::array<uint8_t, 40> line{};
    rb::host::screenLine(" LISTA LLENA: 999 IMAGENES", Span<uint8_t>{line});
    TEST_ASSERT_EQUAL_UINT8_ARRAY(line.data(), reply.screen() + rb::host::kMainMessage, 40);
    TEST_ASSERT_EQUAL_STRING("name=- density=- drive=0 result=full", events.create);
}

// A name inserted before mounted images moves their positions: every drive keeps its name.
static void test_create_keeps_the_names_of_mounted_images(void)
{
    WriteRig rig;
    rig.keys({K1, KB, K3});            // demo-uno.atr, the second entry, on D3
    rig.create("DEMO-T", {KS, K2});    // between demo-dos and demo-uno
    rig.create("AAA", {KM, K4});       // before every entry
    assert_drive_line(rig, 2, "D2 DEMO-T.ATR");
    assert_drive_line(rig, 3, "D3 DEMO-UNO.ATR");
    assert_drive_line(rig, 4, "D4 AAA.ATR");
    TEST_ASSERT_EQUAL_STRING("AAA.ATR", rig.catalog.name(0));
    TEST_ASSERT_EQUAL_STRING("DEMO-T.ATR", rig.catalog.name(2));
    TEST_ASSERT_EQUAL_STRING("demo-uno.atr", rig.catalog.name(3));
    rig.keys({K1, KB, K5});            // B is now demo-dos.atr (A is AAA.ATR)
    assert_drive_line(rig, 5, "D5 DEMO-DOS.ATR");
}

static void test_crc32_is_the_ieee_one(void)
{
    const char* text = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, rb::host::crc32(reinterpret_cast<const uint8_t*>(text), 9));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_put_and_write_are_read_back_and_survive_a_restart);
    RUN_TEST(test_retried_write_leaves_the_file_as_one_write);
    RUN_TEST(test_remounting_the_same_atr_keeps_it_writable);
    RUN_TEST(test_read_only_file_is_protected_marked_and_unchanged);
    RUN_TEST(test_same_atr_on_two_drives_only_the_first_writes);
    RUN_TEST(test_menu_on_d1_refuses_writes);
    RUN_TEST(test_tnfs_image_is_protected);
    RUN_TEST(test_failed_save_answers_error_with_cause_io);
    RUN_TEST(test_naks_are_logged);
    RUN_TEST(test_lost_completion_is_still_logged);
    RUN_TEST(test_create_mounts_a_new_single_density_atr);
    RUN_TEST(test_format_single_and_medium_write_zeros_and_are_logged);
    RUN_TEST(test_format_with_the_other_density_is_an_error);
    RUN_TEST(test_format_refused_on_protected_and_empty_drives);
    RUN_TEST(test_create_refuses_an_existing_name);
    RUN_TEST(test_create_refuses_an_invalid_name);
    RUN_TEST(test_create_takes_lower_case_as_capitals);
    RUN_TEST(test_create_cancels_and_refuses_d1);
    RUN_TEST(test_create_refuses_a_full_list);
    RUN_TEST(test_create_keeps_the_names_of_mounted_images);
    RUN_TEST(test_crc32_is_the_ieee_one);
    return UNITY_END();
}
