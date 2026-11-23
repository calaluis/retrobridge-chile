// TnfsLoader (menu-tnfs T011, AC5, AC16, AC17, AC18, AC20, AC21, M41-1, M41-6):
// one loader thread, one job at a time with a number, cancel, and results of
// cancelled or replaced jobs discarded with their slot freed, also when the
// cancel is forced into the window around the publication.
//
// The TNFS server is FakeTnfsServer on a fake clock, so its waits cost no real
// time. A GatedPort holds chosen requests until the test lets them go, which
// makes the in-progress states observable without timing guesses.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <new>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "fakes/fake_clock.hpp"
#include "fakes/fake_tnfs_server.hpp"
#include "fakes/gated_port.hpp"
#include "rb/menu/memory_image_store.hpp"
#include "rb/net_tnfs/tnfs_codec.hpp"
#include "rb/menu/sha256.hpp"
#include "rb/menu/tnfs_folder.hpp"
#include "tnfs_loader.hpp"
#include "unity.h"

// Allocations per thread (menu-navegacion N002): poll() runs on the menu thread and
// must not allocate; the loader thread's allocations are not counted here.
thread_local unsigned long t_allocations = 0;

void* operator new(std::size_t size)
{
    ++t_allocations;
    void* p = std::malloc(size == 0 ? 1 : size);
    if (p == nullptr) {
        std::abort();
    }
    return p;
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    ++t_allocations;
    return std::malloc(size == 0 ? 1 : size);
}
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept { return ::operator new(size, tag); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace tnfs = rb::net_tnfs;
using rb::core::ErrorCode;
using rb::test::FakeClock;
using rb::test::FakeTnfsServer;
using rb::test::GatedPort;
using rb::core::Span;
using rb::host::ImageMemory;
using rb::host::LoaderError;
using rb::host::LoaderStatus;
using rb::host::MemoryImageStore;
using rb::host::TnfsFolder;
using rb::host::TnfsLink;
using rb::host::TnfsLoader;
using Kind = LoaderStatus::Kind;
using Bytes = std::vector<uint8_t>;

namespace {

FakeClock fakeClock;  // used by the loader thread only, while a loader runs

struct Clock final : tnfs::TnfsClock {
    uint64_t nowMicros() const override { return fakeClock.nowMicros(); }
    void sleepUntil(uint64_t micros) override { fakeClock.waitUntil(micros); }
};

struct FakeLink final : TnfsLink {
    explicit FakeLink(rb::netsio::DatagramPort& p) : port_(p) {}
    rb::core::Result<void> open() override
    {
        ++opens;
        if (failWith) {
            return rb::core::Failure{*failWith};
        }
        return {};
    }
    rb::netsio::DatagramPort& port() override { return port_; }
    void rename(const char* server) noexcept override
    {
        std::snprintf(renamedTo, sizeof renamedTo, "%s", server);
        ++renames;
    }
    rb::netsio::DatagramPort& port_;
    std::optional<ErrorCode> failWith;
    int opens = 0;
    int renames = 0;
    char renamedTo[33] = {};
};

// Heap memory that can be made to fail, or to wait until the test lets it go.
struct TestMemory final : ImageMemory {
    bool allocate(rb::host::ImageBuffer& out, uint32_t size) override
    {
        {
            std::unique_lock<std::mutex> lock(mutex);
            asked = size;
            entered = true;
            changed.notify_all();
            changed.wait(lock, [this] { return !hold; });
        }
        if (fail) {
            return false;
        }
        out = rb::host::ImageBuffer::allocate(size);
        return out.valid();
    }
    void release()
    {
        std::lock_guard<std::mutex> lock(mutex);
        hold = false;
        changed.notify_all();
    }
    void awaitEntered()
    {
        std::unique_lock<std::mutex> lock(mutex);
        TEST_ASSERT_TRUE(changed.wait_for(lock, std::chrono::seconds(10), [this] { return entered; }));
    }
    bool fail = false;
    bool hold = false;
    bool entered = false;
    uint32_t asked = 0;
    std::mutex mutex;
    std::condition_variable changed;
};

// Records the publication windows and optionally acts inside them.
struct Hooks final : rb::host::LoaderHooks {
    void beforePublish(uint32_t job) override
    {
        if (onBefore) {
            onBefore(job);
        }
    }
    void afterPublish(uint32_t job) override
    {
        std::lock_guard<std::mutex> lock(mutex);
        published.push_back(job);
        changed.notify_all();
    }
    void awaitPublished(uint32_t job)
    {
        std::unique_lock<std::mutex> lock(mutex);
        TEST_ASSERT_TRUE(changed.wait_for(lock, std::chrono::seconds(10), [&] {
            return std::find(published.begin(), published.end(), job) != published.end();
        }));
    }
    std::function<void(uint32_t)> onBefore;
    std::vector<uint32_t> published;
    std::mutex mutex;
    std::condition_variable changed;
};

rb::host::ImageBuffer buffer(const Bytes& bytes)
{
    return rb::host::ImageBuffer::copyOf(Span<const uint8_t>(bytes.data(), bytes.size()));
}

Bytes image(std::size_t size)
{
    Bytes bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<uint8_t>(i * 13 + 7);
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

// Waits (real time) until the store holds `count` slots: frees happen on the loader thread.
void awaitSlots(const MemoryImageStore& store, std::size_t count)
{
    for (int i = 0; i < 10000 && slotsInUse(store) != count; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    TEST_ASSERT_EQUAL_size_t(count, slotsInUse(store));
}

std::string pathOf(const TnfsFolder& folder, std::size_t index)
{
    char out[TnfsFolder::kMaxPath + 1];
    folder.path(index, out);
    return out;
}

LoaderStatus awaitResult(TnfsLoader& loader, uint32_t job, TnfsFolder& folder)
{
    for (int i = 0; i < 10000; ++i) {
        const LoaderStatus status = loader.poll(job, folder);
        if (status.kind != Kind::InProgress) {
            return status;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    TEST_FAIL_MESSAGE("the job did not end");
    return LoaderStatus{};
}

uint8_t percentNow(TnfsLoader& loader, uint32_t job)
{
    TnfsFolder unused;
    const LoaderStatus status = loader.poll(job, unused);
    TEST_ASSERT_TRUE(status.kind == Kind::InProgress);
    return status.percent;
}

uint32_t bytesNow(TnfsLoader& loader, uint32_t job)
{
    TnfsFolder unused;
    const LoaderStatus status = loader.poll(job, unused);
    TEST_ASSERT_TRUE(status.kind == Kind::InProgress);
    return status.bytes;
}

uint32_t list(TnfsLoader& loader, const char* path, unsigned source = 2)
{
    return loader.startList(source, path, std::strlen(path));
}
uint32_t fetch(TnfsLoader& loader, const char* path, unsigned source = 2)
{
    return loader.startFetch(source, path, std::strlen(path));
}

std::size_t countCommand(const FakeTnfsServer& server, uint8_t command)
{
    std::size_t count = 0;
    for (const auto& entry : server.log()) {
        count += entry.command == command ? 1 : 0;
    }
    return count;
}

// One loader over one server on source 2.
struct Rig {
    Rig() : server(fakeClock), gated(server), link(gated)
    {
        links[2] = &link;
    }
    FakeTnfsServer server;
    GatedPort gated;
    FakeLink link;
    TnfsLink* links[TnfsLoader::kMaxSource + 1] = {};
    MemoryImageStore store;
    TestMemory memory;
    Hooks hooks;
    Clock clock;
};

// menu-tnfs-persistente T4: the restore link. rename() picks one of two fake servers by name, as
// a name resolving to another address would; an unknown name does not resolve.
struct RoutedLink final : TnfsLink, rb::netsio::DatagramPort {
    RoutedLink(FakeTnfsServer& a, FakeTnfsServer& b) : a_(a), b_(b) {}
    rb::core::Result<void> open() override
    {
        ++opens;
        if (current_ == nullptr) {
            return rb::core::Failure{ErrorCode::NotFound};
        }
        return {};
    }
    rb::netsio::DatagramPort& port() override { return *this; }
    void rename(const char* server) noexcept override
    {
        ++renames;
        current_ = std::strcmp(server, "srv-a") == 0 ? &a_ : std::strcmp(server, "srv-b") == 0 ? &b_ : nullptr;
    }
    rb::core::Result<void> send(Span<const uint8_t> datagram) override
    {
        if (current_ == nullptr) {
            return rb::core::Failure{ErrorCode::IoError};
        }
        return current_->send(datagram);
    }
    rb::core::Result<std::size_t> receive(Span<uint8_t> out, uint32_t timeoutUs) override
    {
        if (current_ == nullptr) {
            return rb::core::Failure{ErrorCode::IoError};
        }
        return current_->receive(out, timeoutUs);
    }
    int opens = 0;
    int renames = 0;

private:
    FakeTnfsServer& a_;
    FakeTnfsServer& b_;
    FakeTnfsServer* current_ = nullptr;
};

// Source 2 on srv-a, and the restore link able to reach srv-a and srv-b.
struct TwoServers {
    TwoServers() : a(fakeClock), b(fakeClock), source2(a), restore(a, b) { links[2] = &source2; }
    FakeTnfsServer a;
    FakeTnfsServer b;
    FakeLink source2;
    RoutedLink restore;
    TnfsLink* links[TnfsLoader::kMaxSource + 1] = {};
    MemoryImageStore store;
    TestMemory memory;
    Clock clock;
};

uint32_t fetchAt(TnfsLoader& loader, const char* server, const char* path)
{
    return loader.startFetchAt(server, path, std::strlen(path));
}

rb::host::Sha256Digest digestOf(const Bytes& bytes)
{
    return rb::host::sha256(Span<const uint8_t>(bytes.data(), bytes.size()));
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_lists_a_folder(void)
{
    Rig rig;
    rig.server.addDirectory("/Juegos");
    rig.server.addFile("/Logo.atr", image(100));
    rig.server.addFile("/notas.txt", image(5));
    rig.server.addFile("/.oculto.atr", image(5));
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    TnfsFolder folder;
    const uint32_t job = list(loader, "/");
    TEST_ASSERT_NOT_EQUAL(0, job);
    const LoaderStatus status = awaitResult(loader, job, folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Folder);
    TEST_ASSERT_EQUAL_size_t(2, folder.total());
    TEST_ASSERT_EQUAL_STRING("JUEGOS/", folder.entry(0).display);
    TEST_ASSERT_EQUAL_STRING("/Juegos", pathOf(folder, 0).c_str());
    TEST_ASSERT_EQUAL_STRING("LOGO.ATR", folder.entry(1).display);
    // Every entry but the dotted ones (".", ".." and ".oculto.atr") is asked with STAT.
    TEST_ASSERT_EQUAL_size_t(3, countCommand(rig.server, tnfs::cmd::kStat));
    TEST_ASSERT_EQUAL(1, rig.link.opens);
    TEST_ASSERT_EQUAL_size_t(0, rig.server.openHandles());
    // Handed over once.
    TEST_ASSERT_TRUE(loader.poll(job, folder).kind == Kind::Unknown);
}

// menu-navegacion N002 (AC10, M42-3): the first 999 of 1200 and the total; poll() on
// the menu thread exchanges the storage and allocates nothing.
static void test_poll_hands_over_1200_entries_without_allocating(void)
{
    Rig rig;
    char name[32];
    for (int i = 0; i < 1200; ++i) {
        std::snprintf(name, sizeof name, "/big/G%04d.ATR", i);
        rig.server.addFile(name, image(16));
    }
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    TnfsFolder folder;
    const uint32_t job = list(loader, "/big");
    LoaderStatus status;
    unsigned long allocated = 0;
    for (int i = 0; i < 20000; ++i) {
        const unsigned long before = t_allocations;
        status = loader.poll(job, folder);
        allocated += t_allocations - before;
        if (status.kind != Kind::InProgress) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    TEST_ASSERT_TRUE(status.kind == Kind::Folder);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, allocated, "poll() allocated on the menu thread");
    TEST_ASSERT_EQUAL_size_t(1200, folder.total());
    TEST_ASSERT_EQUAL_size_t(999, folder.count());
    TEST_ASSERT_EQUAL_STRING("G0998.ATR", folder.entry(998).display);
    TEST_ASSERT_EQUAL_STRING("/big/G0998.ATR", pathOf(folder, 998).c_str());
    // A second listing reuses the storage handed back: the loader still has room.
    const uint32_t again = list(loader, "/big");
    TEST_ASSERT_TRUE(awaitResult(loader, again, folder).kind == Kind::Folder);
    TEST_ASSERT_EQUAL_size_t(999, folder.count());
}

static void test_fetches_a_whole_image_into_a_slot_with_its_sha256(void)
{
    Rig rig;
    const Bytes bytes = image(16 + 720 * 128);
    rig.server.addFile("/a/Atari Logo.atr", bytes);
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    TnfsFolder folder;
    const uint32_t job = fetch(loader, "/a/Atari Logo.atr");
    const LoaderStatus status = awaitResult(loader, job, folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Image);
    TEST_ASSERT_TRUE(rig.store.inUse(status.slot));
    TEST_ASSERT_EQUAL_UINT32(bytes.size(), status.bytes);
    TEST_ASSERT_EQUAL_UINT32(bytes.size(), rig.memory.asked);
    rb::domain::ImageByteSource& source = rig.store.source(status.slot);
    TEST_ASSERT_EQUAL_UINT32(bytes.size(), source.size());
    Bytes read(bytes.size());
    TEST_ASSERT_TRUE(source.readAt(0, Span<uint8_t>(read.data(), read.size())).ok());
    TEST_ASSERT_TRUE(read == bytes);
    const rb::host::Sha256Digest expected = rb::host::sha256(Span<const uint8_t>(bytes.data(), bytes.size()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), status.sha256.data(), 32);
    for (const auto& entry : rig.server.log()) {
        if (entry.isOpen) {
            TEST_ASSERT_EQUAL_HEX16(0x0001, entry.flags);
            TEST_ASSERT_EQUAL_HEX16(0, entry.mode);
        }
    }
    TEST_ASSERT_TRUE(loader.poll(job, folder).kind == Kind::Unknown);
    TEST_ASSERT_EQUAL_size_t(1, slotsInUse(rig.store));  // the caller owns it now
}

static void test_job_numbers_grow_and_unknown_numbers_say_so(void)
{
    Rig rig;
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    TnfsFolder folder;
    TEST_ASSERT_TRUE(loader.poll(0, folder).kind == Kind::Unknown);
    TEST_ASSERT_TRUE(loader.poll(77, folder).kind == Kind::Unknown);
    const uint32_t first = list(loader, "/");
    TEST_ASSERT_TRUE(awaitResult(loader, first, folder).kind == Kind::Folder);
    const uint32_t second = list(loader, "/");
    TEST_ASSERT_TRUE(second > first);
    TEST_ASSERT_TRUE(loader.poll(first, folder).kind == Kind::Unknown);
    TEST_ASSERT_TRUE(awaitResult(loader, second, folder).kind == Kind::Folder);
}

static void test_list_percentage_is_0_during_readdir_then_follows_the_stats(void)
{
    Rig rig;
    rig.server.addFile("/a.atr", image(10));
    rig.server.addFile("/b.atr", image(10));
    rig.server.addFile("/c.txt", image(10));
    rig.server.addFile("/.d.atr", image(10));
    rig.gated.gate(tnfs::cmd::kStat);
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    const uint32_t job = list(loader, "/");
    rig.gated.awaitHeld();  // READDIR is over, the first STAT is held
    TEST_ASSERT_EQUAL_UINT8(0, percentNow(loader, job));
    rig.gated.permit();
    rig.gated.awaitHeld();
    TEST_ASSERT_EQUAL_UINT8(33, percentNow(loader, job));  // 1 of 3
    rig.gated.permit();
    rig.gated.awaitHeld();
    TEST_ASSERT_EQUAL_UINT8(66, percentNow(loader, job));
    rig.gated.open();
    TnfsFolder folder;
    TEST_ASSERT_TRUE(awaitResult(loader, job, folder).kind == Kind::Folder);
    TEST_ASSERT_EQUAL_size_t(2, folder.total());
}

static void test_fetch_percentage_follows_the_bytes_and_stops_at_99(void)
{
    Rig rig;
    rig.server.addFile("/x.atr", image(4 * 512));
    rig.gated.gate(tnfs::cmd::kRead);
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    const uint32_t job = fetch(loader, "/x.atr");
    const uint8_t expected[] = {0, 25, 50, 75};
    uint32_t bytes = 0;
    for (uint8_t percent : expected) {
        rig.gated.awaitHeld();
        TEST_ASSERT_EQUAL_UINT8(percent, percentNow(loader, job));
        TEST_ASSERT_EQUAL_UINT32(bytes, bytesNow(loader, job));  // menu-tnfs-persistente T3
        bytes += 512;
        rig.gated.permit();
    }
    rig.gated.open();
    TnfsFolder folder;
    TEST_ASSERT_TRUE(awaitResult(loader, job, folder).kind == Kind::Image);
}

static void test_cancel_while_running_publishes_nothing_and_closes_on_the_server(void)
{
    Rig rig;
    rig.server.addFile("/x.atr", image(4 * 512));
    rig.gated.gate(tnfs::cmd::kRead);
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    const uint32_t job = fetch(loader, "/x.atr");
    rig.gated.awaitHeld();
    loader.cancel(job);
    TnfsFolder folder;
    TEST_ASSERT_TRUE(loader.poll(job, folder).kind == Kind::Unknown);
    rig.gated.open();
    rig.hooks.awaitPublished(job);
    TEST_ASSERT_TRUE(loader.poll(job, folder).kind == Kind::Unknown);
    TEST_ASSERT_EQUAL_size_t(0, slotsInUse(rig.store));
    TEST_ASSERT_EQUAL_size_t(0, rig.server.openHandles());
    TEST_ASSERT_EQUAL_size_t(1, countCommand(rig.server, tnfs::cmd::kClose));
}

static void test_a_new_job_replaces_the_running_one(void)
{
    Rig rig;
    rig.server.addFile("/a.atr", image(1000));
    rig.server.addFile("/b.atr", image(2000));
    rig.memory.hold = true;
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    const uint32_t first = fetch(loader, "/a.atr");
    rig.memory.awaitEntered();  // the first job is inside the loader thread
    const uint32_t second = fetch(loader, "/b.atr");
    rig.memory.release();
    TnfsFolder folder;
    const LoaderStatus status = awaitResult(loader, second, folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Image);
    TEST_ASSERT_EQUAL_UINT32(2000, status.bytes);
    TEST_ASSERT_TRUE(loader.poll(first, folder).kind == Kind::Unknown);
    TEST_ASSERT_EQUAL_size_t(1, slotsInUse(rig.store));
    TEST_ASSERT_EQUAL_size_t(0, rig.server.openHandles());
    // The replaced job stopped before its first READ: only the 4 of /b.atr.
    TEST_ASSERT_EQUAL_size_t(4, countCommand(rig.server, tnfs::cmd::kRead));
}

// The bad order of AC18: the cancel lands after the image is in its slot and
// before it is published.
static void test_a_cancel_just_before_publication_frees_the_slot(void)
{
    Rig rig;
    rig.server.addFile("/x.atr", image(3000));
    TnfsLoader* self = nullptr;
    std::size_t slotsBefore = 99;  // read on the loader thread, checked here
    rig.hooks.onBefore = [&self, &rig, &slotsBefore](uint32_t job) {
        slotsBefore = slotsInUse(rig.store);  // adopted, not yet published
        self->cancel(job);
    };
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    self = &loader;
    const uint32_t job = fetch(loader, "/x.atr");
    rig.hooks.awaitPublished(job);
    TEST_ASSERT_EQUAL_size_t(1, slotsBefore);
    TnfsFolder folder;
    TEST_ASSERT_TRUE(loader.poll(job, folder).kind == Kind::Unknown);
    awaitSlots(rig.store, 0);
}

// The same with a replacement in that window: the late result is dropped and
// the new job still runs.
static void test_a_replacement_just_before_publication_frees_the_slot(void)
{
    Rig rig;
    rig.server.addFile("/x.atr", image(3000));
    rig.server.addFile("/y.atr", image(500));
    TnfsLoader* self = nullptr;
    uint32_t second = 0;
    rig.hooks.onBefore = [&](uint32_t job) {
        if (second == 0) {
            second = self->startFetch(2, "/y.atr", 6);
            (void)job;
        }
    };
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    self = &loader;
    const uint32_t first = fetch(loader, "/x.atr");
    rig.hooks.awaitPublished(first);
    TnfsFolder folder;
    TEST_ASSERT_TRUE(loader.poll(first, folder).kind == Kind::Unknown);
    const LoaderStatus status = awaitResult(loader, second, folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Image);
    TEST_ASSERT_EQUAL_UINT32(500, status.bytes);
    awaitSlots(rig.store, 1);
}

// A published result nobody took is freed when its job is cancelled.
static void test_a_cancel_after_publication_frees_the_slot_on_the_loader_thread(void)
{
    Rig rig;
    rig.server.addFile("/x.atr", image(3000));
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    const uint32_t job = fetch(loader, "/x.atr");
    rig.hooks.awaitPublished(job);
    TEST_ASSERT_EQUAL_size_t(1, slotsInUse(rig.store));
    loader.cancel(job);
    awaitSlots(rig.store, 0);
    TnfsFolder folder;
    TEST_ASSERT_TRUE(loader.poll(job, folder).kind == Kind::Unknown);
}

static void test_a_mute_server_gives_no_responde(void)
{
    Rig rig;
    for (int i = 0; i < 3; ++i) {
        rig.server.silence(tnfs::cmd::kMount);
    }
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    TnfsFolder folder;
    const LoaderStatus status = awaitResult(loader, list(loader, "/"), folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Error);
    TEST_ASSERT_TRUE(status.error == LoaderError::NoResponde);
    TEST_ASSERT_TRUE(std::strlen(status.cause) > 0);
}

static void test_a_malformed_answer_gives_no_responde(void)
{
    Rig rig;
    for (int i = 0; i < 3; ++i) {
        rig.server.malformed(tnfs::cmd::kMount);
    }
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    TnfsFolder folder;
    const LoaderStatus status = awaitResult(loader, list(loader, "/"), folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Error);
    TEST_ASSERT_TRUE(status.error == LoaderError::NoResponde);
}

static void test_a_name_that_does_not_resolve_gives_no_encontrado_and_is_tried_again(void)
{
    Rig rig;
    rig.link.failWith = ErrorCode::NotFound;
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    TnfsFolder folder;
    LoaderStatus status = awaitResult(loader, list(loader, "/"), folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Error);
    TEST_ASSERT_TRUE(status.error == LoaderError::NoEncontrado);
    rig.link.failWith.reset();
    status = awaitResult(loader, list(loader, "/"), folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Folder);
    TEST_ASSERT_EQUAL(2, rig.link.opens);
}

// Demo of menu-editar-servidor: the DNS record of a server was fixed while the host ran.
// After no answer, the next job opens the link again, so the name is resolved again.
static void test_a_silent_server_is_opened_again_by_the_next_job(void)
{
    Rig rig;
    for (int i = 0; i < 3; ++i) {
        rig.server.silence(tnfs::cmd::kMount);
    }
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    TnfsFolder folder;
    LoaderStatus status = awaitResult(loader, list(loader, "/"), folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Error);
    TEST_ASSERT_TRUE(status.error == LoaderError::NoResponde);
    TEST_ASSERT_EQUAL(1, rig.link.opens);
    status = awaitResult(loader, list(loader, "/"), folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Folder);
    TEST_ASSERT_EQUAL(2, rig.link.opens);
    // An answered job keeps the link: no third open.
    status = awaitResult(loader, list(loader, "/"), folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Folder);
    TEST_ASSERT_EQUAL(2, rig.link.opens);
}

static void test_a_link_that_cannot_open_gives_no_responde(void)
{
    Rig rig;
    rig.link.failWith = ErrorCode::TransportError;
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    TnfsFolder folder;
    const LoaderStatus status = awaitResult(loader, list(loader, "/"), folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Error);
    TEST_ASSERT_TRUE(status.error == LoaderError::NoResponde);
}

static void test_a_source_without_server_gives_no_encontrado(void)
{
    Rig rig;
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    TnfsFolder folder;
    for (unsigned source : {0u, 1u, 3u, 9u}) {
        const LoaderStatus status = awaitResult(loader, list(loader, "/", source), folder);
        TEST_ASSERT_TRUE(status.kind == Kind::Error);
        TEST_ASSERT_TRUE(status.error == LoaderError::NoEncontrado);
    }
}

// menu-tnfs-persistente T3: a missing file is NoExiste (STAT answered ENOENT); a missing folder
// of a listing stays NoSePudoAbrir (the listing is no assignment to forget).
static void test_a_missing_file_gives_no_existe_and_a_missing_folder_no_se_pudo_abrir(void)
{
    Rig rig;
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    TnfsFolder folder;
    LoaderStatus status = awaitResult(loader, fetch(loader, "/nada.atr"), folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Error);
    TEST_ASSERT_TRUE(status.error == LoaderError::NoExiste);
    status = awaitResult(loader, list(loader, "/nada"), folder);
    TEST_ASSERT_TRUE(status.error == LoaderError::NoSePudoAbrir);
    TEST_ASSERT_EQUAL_size_t(0, slotsInUse(rig.store));
}

static void test_no_memory_gives_no_cabe_with_the_cause(void)
{
    Rig rig;
    rig.server.addFile("/grande.atr", image(70000));
    rig.memory.fail = true;
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    TnfsFolder folder;
    const LoaderStatus status = awaitResult(loader, fetch(loader, "/grande.atr"), folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Error);
    TEST_ASSERT_TRUE(status.error == LoaderError::NoCabe);
    TEST_ASSERT_NOT_NULL(std::strstr(status.cause, "70000"));
    TEST_ASSERT_EQUAL_size_t(0, slotsInUse(rig.store));
    TEST_ASSERT_EQUAL_size_t(0, rig.server.openHandles());
}

static void test_no_free_slot_gives_no_cabe(void)
{
    Rig rig;
    rig.server.addFile("/x.atr", image(100));
    for (std::size_t i = 0; i < MemoryImageStore::kSlots; ++i) {
        TEST_ASSERT_TRUE(rig.store.adopt(buffer(Bytes(8))).ok());
    }
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    TnfsFolder folder;
    const LoaderStatus status = awaitResult(loader, fetch(loader, "/x.atr"), folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Error);
    TEST_ASSERT_TRUE(status.error == LoaderError::NoCabe);
    TEST_ASSERT_TRUE(std::strlen(status.cause) > 0);
}

// menu-tnfs-persistente T3 (AC5, plan R7): ENOENT to OPEN gives NoExiste too; EACCES, another
// status, a folder or ENOENT to READ stay NoSePudoAbrir, which never forgets an assignment.
static void test_only_a_confirmed_enoent_gives_no_existe(void)
{
    Rig rig;
    rig.server.addFile("/x.atr", image(1200));
    rig.server.addDirectory("/dir");
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    TnfsFolder folder;
    struct Case {
        uint8_t command;
        uint8_t code;
        LoaderError error;
    };
    const Case cases[] = {
        {tnfs::cmd::kOpen, 0x02, LoaderError::NoExiste},       // ENOENT to OPEN
        {tnfs::cmd::kOpen, 0x09, LoaderError::NoSePudoAbrir},  // EACCES
        {tnfs::cmd::kStat, 0x09, LoaderError::NoSePudoAbrir},  // EACCES to STAT
        {tnfs::cmd::kStat, 0x03, LoaderError::NoSePudoAbrir},  // EIO: another status
        {tnfs::cmd::kRead, 0x02, LoaderError::NoSePudoAbrir},  // ENOENT to READ: not about the path
    };
    for (const Case& c : cases) {
        rig.server.reject(c.command, c.code);
        const LoaderStatus status = awaitResult(loader, fetch(loader, "/x.atr"), folder);
        TEST_ASSERT_TRUE(status.kind == Kind::Error);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(c.error), static_cast<int>(status.error));
    }
    // A folder after an ENOENT: no status this time, so no NoExiste left over.
    LoaderStatus status = awaitResult(loader, fetch(loader, "/nada.atr"), folder);
    TEST_ASSERT_TRUE(status.error == LoaderError::NoExiste);
    status = awaitResult(loader, fetch(loader, "/dir"), folder);
    TEST_ASSERT_TRUE(status.error == LoaderError::NoSePudoAbrir);
    TEST_ASSERT_TRUE(awaitResult(loader, fetch(loader, "/x.atr"), folder).kind == Kind::Image);
    TEST_ASSERT_EQUAL_size_t(0, rig.server.openHandles());
}

static void test_a_path_too_long_gives_no_se_pudo_abrir(void)
{
    Rig rig;
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    const std::string path = "/" + std::string(tnfs::kMaxPath, 'x');
    TnfsFolder folder;
    const LoaderStatus status = awaitResult(loader, loader.startList(2, path.data(), path.size()), folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Error);
    TEST_ASSERT_TRUE(status.error == LoaderError::NoSePudoAbrir);
    TEST_ASSERT_EQUAL_size_t(0, rig.server.log().size());
}

// The menu thread is never held by a job: start, poll and cancel answer while
// the loader thread is stuck inside a request.
static void test_start_poll_and_cancel_answer_while_a_request_is_held(void)
{
    Rig rig;
    rig.server.addFile("/x.atr", image(1000));
    rig.gated.gate(tnfs::cmd::kRead);
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    const uint32_t job = fetch(loader, "/x.atr");
    rig.gated.awaitHeld();
    const auto begin = std::chrono::steady_clock::now();
    TnfsFolder folder;
    TEST_ASSERT_TRUE(loader.poll(job, folder).kind == Kind::InProgress);
    const uint32_t next = list(loader, "/");
    loader.cancel(next);
    const auto took = std::chrono::steady_clock::now() - begin;
    TEST_ASSERT_TRUE(took < std::chrono::milliseconds(250));
    rig.gated.open();
}

static void test_the_loader_stops_with_a_job_in_progress(void)
{
    Rig rig;
    for (int i = 0; i < 3; ++i) {
        rig.server.silence(tnfs::cmd::kMount);
    }
    {
        TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
        (void)list(loader, "/");
    }
    TEST_ASSERT_EQUAL_size_t(0, slotsInUse(rig.store));
}

// After a silent reconnection the image is read again from offset 0; the
// percentage does not go down, and the bytes are still right.
static void test_the_percentage_never_goes_down_after_a_reconnection(void)
{
    Rig rig;
    const Bytes bytes = image(4 * 512);
    rig.server.addFile("/x.atr", bytes);
    rig.gated.gate(tnfs::cmd::kRead);
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    const uint32_t job = fetch(loader, "/x.atr");
    rig.gated.awaitHeld();
    rig.gated.permit();
    rig.gated.awaitHeld();
    rig.gated.permit();
    rig.gated.awaitHeld();  // third READ held: two blocks read
    TEST_ASSERT_EQUAL_UINT8(50, percentNow(loader, job));
    TEST_ASSERT_EQUAL_UINT32(1024, bytesNow(loader, job));
    rig.server.forgetSession();  // the loader thread is held in the gate, not in the server
    rig.gated.permit();
    rig.gated.awaitHeld();  // first READ of the second pass
    TEST_ASSERT_EQUAL_UINT8(50, percentNow(loader, job));
    rig.gated.permit();
    rig.gated.awaitHeld();  // one block of the second pass: 25 would be lower
    TEST_ASSERT_EQUAL_UINT8(50, percentNow(loader, job));
    // The bytes received count what arrives: the re-read block too (T3). They never stand still
    // while data comes, so the restore queue's 30 s of silence cannot start here.
    TEST_ASSERT_EQUAL_UINT32(1536, bytesNow(loader, job));
    rig.gated.open();
    TnfsFolder folder;
    const LoaderStatus status = awaitResult(loader, job, folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Image);
    const rb::host::Sha256Digest expected = rb::host::sha256(Span<const uint8_t>(bytes.data(), bytes.size()));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), status.sha256.data(), 32);
}

// A STAT the server refuses drops that entry; a STAT with no answer fails the listing.
static void test_a_refused_stat_drops_the_entry_and_a_mute_one_fails(void)
{
    Rig rig;
    rig.server.addFile("/a.atr", image(10));
    rig.server.addFile("/b.atr", image(10));
    rig.server.reject(tnfs::cmd::kStat, FakeTnfsServer::kNoEntry);
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    TnfsFolder folder;
    LoaderStatus status = awaitResult(loader, list(loader, "/"), folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Folder);
    TEST_ASSERT_EQUAL_size_t(1, folder.total());
    for (int i = 0; i < 3; ++i) {
        rig.server.silence(tnfs::cmd::kStat);
    }
    status = awaitResult(loader, list(loader, "/"), folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Error);
    TEST_ASSERT_TRUE(status.error == LoaderError::NoResponde);
}

// The session is lost half way through READDIR: the listing starts again and
// no name is counted twice.
static void test_a_listing_restarted_by_a_reconnection_counts_each_entry_once(void)
{
    Rig rig;
    rig.server.addFile("/a.atr", image(10));
    rig.server.addFile("/b.atr", image(10));
    rig.server.addFile("/c.atr", image(10));
    rig.gated.gate(tnfs::cmd::kReadDir);
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    const uint32_t job = list(loader, "/");
    for (int i = 0; i < 3; ++i) {
        rig.gated.awaitHeld();
        rig.gated.permit();
    }
    rig.gated.awaitHeld();  // the fourth READDIR: at least one real name has arrived
    rig.server.forgetSession();
    rig.gated.open();
    TnfsFolder folder;
    const LoaderStatus status = awaitResult(loader, job, folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Folder);
    TEST_ASSERT_EQUAL_size_t(3, folder.total());
    TEST_ASSERT_EQUAL_size_t(2, countCommand(rig.server, tnfs::cmd::kMount));
}

// M41-20: the menu gives slots back; the loader thread frees them, and a slot
// still open keeps its mark until a later wake finds it closed.
static void test_recycled_slots_are_freed_on_the_loader_thread_and_a_held_one_waits(void)
{
    Rig rig;
    rig.server.addFile("/a.atr", image(300));
    rig.server.addFile("/b.atr", image(200));
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    TnfsFolder folder;
    const LoaderStatus a = awaitResult(loader, fetch(loader, "/a.atr"), folder);
    const LoaderStatus b = awaitResult(loader, fetch(loader, "/b.atr"), folder);
    TEST_ASSERT_TRUE(a.kind == Kind::Image && b.kind == Kind::Image);
    awaitSlots(rig.store, 2);
    const auto held = rig.store.open(a.slot);
    TEST_ASSERT_TRUE(held.ok());
    loader.recycle(a.slot);  // open: stays
    loader.recycle(b.slot);  // free to go
    awaitSlots(rig.store, 1);
    TEST_ASSERT_TRUE(rig.store.inUse(a.slot));
    rig.store.close(*held.value());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    TEST_ASSERT_TRUE(rig.store.inUse(a.slot));  // nothing woke the loader yet
    const uint32_t job = list(loader, "/");     // the next job's wake frees it
    TEST_ASSERT_TRUE(awaitResult(loader, job, folder).kind == Kind::Folder);
    awaitSlots(rig.store, 0);
    loader.recycle(a.slot);                     // a free slot drops the mark
    loader.recycle(MemoryImageStore::kSlots);   // out of range: ignored
    TEST_ASSERT_TRUE(awaitResult(loader, list(loader, "/"), folder).kind == Kind::Folder);
    awaitSlots(rig.store, 0);
}

// await() returns when the job ends, and otherwise after its time.
static void test_await_returns_at_the_end_of_a_job_or_after_its_time(void)
{
    Rig rig;
    rig.server.addFile("/a.atr", image(300));
    rig.gated.gate(tnfs::cmd::kRead);
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    TnfsFolder folder;
    const uint32_t job = fetch(loader, "/a.atr");
    rig.gated.awaitHeld();
    auto begin = std::chrono::steady_clock::now();
    loader.await(job, 50000);
    auto took = std::chrono::steady_clock::now() - begin;
    TEST_ASSERT_TRUE(took >= std::chrono::milliseconds(40));
    TEST_ASSERT_TRUE(loader.poll(job, folder).kind == Kind::InProgress);
    rig.gated.open();
    begin = std::chrono::steady_clock::now();
    loader.await(job, 5000000);
    took = std::chrono::steady_clock::now() - begin;
    TEST_ASSERT_TRUE(took < std::chrono::seconds(4));
    TEST_ASSERT_TRUE(loader.poll(job, folder).kind == Kind::Image);
    loader.await(job, 5000000);  // no longer current: at once
}

// Review of PR #44: closing the loader ends each session it opened, and only those.
static void test_closing_the_loader_unmounts_each_open_session(void)
{
    Rig rig;
    rig.server.addFile("/Logo.atr", image(100));
    {
        TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
        TnfsFolder folder;
        const uint32_t job = list(loader, "/");
        TEST_ASSERT_TRUE(awaitResult(loader, job, folder).kind == Kind::Folder);
        TEST_ASSERT_EQUAL_size_t(1, countCommand(rig.server, tnfs::cmd::kMount));
        TEST_ASSERT_EQUAL_size_t(0, countCommand(rig.server, tnfs::cmd::kUmount));
    }
    TEST_ASSERT_EQUAL_size_t(1, countCommand(rig.server, tnfs::cmd::kUmount));
}

static void test_closing_an_unused_loader_sends_nothing(void)
{
    Rig rig;
    { TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks); }
    TEST_ASSERT_EQUAL_size_t(0, rig.server.log().size());
}

// Review of PR #44, round 1: a server that went silent must not hold the
// shutdown. One UMOUNT goes out and nobody waits for its answer.
static void test_closing_does_not_wait_for_a_silent_server(void)
{
    Rig rig;
    rig.server.addFile("/Logo.atr", image(100));
    std::optional<TnfsLoader> loader;
    loader.emplace(rig.links, rig.store, rig.clock, rig.memory, &rig.hooks);
    TnfsFolder folder;
    const uint32_t job = list(*loader, "/");
    TEST_ASSERT_TRUE(awaitResult(*loader, job, folder).kind == Kind::Folder);
    rig.server.silence(tnfs::cmd::kUmount);
    rig.server.silence(tnfs::cmd::kUmount);
    rig.server.silence(tnfs::cmd::kUmount);
    const uint64_t before = fakeClock.nowMicros();
    loader.reset();
    TEST_ASSERT_EQUAL_size_t(1, countCommand(rig.server, tnfs::cmd::kUmount));
    TEST_ASSERT_TRUE(fakeClock.nowMicros() - before < 100000);
}

// menu-tnfs-persistente T4 (AC9): D2 remembered from srv-a; the user points source 2 at srv-b
// with E; the restore still asks srv-a, on its own session, and srv-b hears nothing.
static void test_a_restore_asks_the_remembered_server_not_the_source(void)
{
    TwoServers rig;
    const Bytes fromA = image(1200);
    Bytes fromB = image(1200);
    fromB[0] ^= 0xFF;
    rig.a.addFile("/juegos/ARCADE.ATR", fromA);
    rig.b.addFile("/juegos/ARCADE.ATR", fromB);
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, nullptr, nullptr, &rig.restore);
    loader.changeServer(2, "srv-b");
    TnfsFolder folder;
    const LoaderStatus status = awaitResult(loader, fetchAt(loader, "srv-a", "/juegos/ARCADE.ATR"), folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Image);
    const rb::host::Sha256Digest expected = digestOf(fromA);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), status.sha256.data(), 32);
    TEST_ASSERT_EQUAL_size_t(0, rig.b.log().size());       // srv-b was never asked
    TEST_ASSERT_EQUAL_INT(0, rig.source2.opens);            // source 2's session was not used
    TEST_ASSERT_EQUAL_STRING("srv-b", rig.source2.renamedTo);  // and it did follow E
    TEST_ASSERT_EQUAL_INT(1, rig.restore.opens);
    (void)rig.store.release(status.slot);
}

// The restore session is kept for the same server and moved, with one UMOUNT, for another.
static void test_the_restore_session_follows_the_server_of_each_job(void)
{
    TwoServers rig;
    rig.a.addFile("/x.atr", image(300));
    rig.b.addFile("/y.atr", image(300));
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, nullptr, nullptr, &rig.restore);
    TnfsFolder folder;
    for (int i = 0; i < 2; ++i) {
        const LoaderStatus status = awaitResult(loader, fetchAt(loader, "srv-a", "/x.atr"), folder);
        TEST_ASSERT_TRUE(status.kind == Kind::Image);
        (void)rig.store.release(status.slot);
    }
    TEST_ASSERT_EQUAL_size_t(1, countCommand(rig.a, tnfs::cmd::kMount));  // one session, kept
    TEST_ASSERT_EQUAL_INT(1, rig.restore.renames);
    const LoaderStatus other = awaitResult(loader, fetchAt(loader, "srv-b", "/y.atr"), folder);
    TEST_ASSERT_TRUE(other.kind == Kind::Image);
    (void)rig.store.release(other.slot);
    TEST_ASSERT_EQUAL_size_t(1, countCommand(rig.a, tnfs::cmd::kUmount));  // srv-a's session ended
    TEST_ASSERT_EQUAL_size_t(1, countCommand(rig.b, tnfs::cmd::kMount));
    TEST_ASSERT_EQUAL_INT(2, rig.restore.renames);
    TEST_ASSERT_EQUAL_size_t(0, rig.a.openHandles());
    TEST_ASSERT_EQUAL_size_t(0, rig.b.openHandles());
}

// startXexAt: the same server rule; the result is the wrapped disk, as startXex gives.
static void test_a_xex_comes_from_the_remembered_server(void)
{
    TwoServers rig;
    const Bytes xex = {0xFF, 0xFF, 0x00, 0x30, 0x00, 0x30, 0x60, 0xE0, 0x02, 0xE1, 0x02, 0x00, 0x30};
    rig.a.addFile("/xex/JUEGO.XEX", xex);
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, nullptr, nullptr, &rig.restore);
    TnfsFolder folder;
    const char* path = "/xex/JUEGO.XEX";
    const LoaderStatus status = awaitResult(loader, loader.startXexAt("srv-a", path, std::strlen(path)), folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Image);
    TEST_ASSERT_TRUE(status.xex);
    TEST_ASSERT_EQUAL_UINT32(2, status.segments);
    TEST_ASSERT_EQUAL_UINT32(xex.size(), status.bytes);
    (void)rig.store.release(status.slot);
    TEST_ASSERT_EQUAL_size_t(0, rig.b.log().size());
}

// No usable server, a name that does not resolve, or no restore link: NoEncontrado (SERVIDOR NO
// ENCONTRADO), and nothing goes out.
static void test_a_restore_without_a_usable_server_gives_no_encontrado(void)
{
    TwoServers rig;
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, nullptr, nullptr, &rig.restore);
    TnfsFolder folder;
    const std::string tooLong(TnfsLoader::kMaxServer + 1, 's');
    const char* const servers[] = {nullptr, "", tooLong.c_str()};
    for (const char* server : servers) {
        const LoaderStatus status = awaitResult(loader, loader.startFetchAt(server, "/x.atr", 6), folder);
        TEST_ASSERT_TRUE(status.kind == Kind::Error);
        TEST_ASSERT_TRUE(status.error == LoaderError::NoEncontrado);
    }
    TEST_ASSERT_EQUAL_INT(0, rig.restore.opens);
    const LoaderStatus unknown = awaitResult(loader, fetchAt(loader, "srv-z", "/x.atr"), folder);
    TEST_ASSERT_TRUE(unknown.error == LoaderError::NoEncontrado);  // the name does not resolve
    TEST_ASSERT_EQUAL_size_t(0, rig.a.log().size() + rig.b.log().size());

    TnfsLoader noLink(rig.links, rig.store, rig.clock, rig.memory);
    const LoaderStatus none = awaitResult(noLink, fetchAt(noLink, "srv-a", "/x.atr"), folder);
    TEST_ASSERT_TRUE(none.error == LoaderError::NoEncontrado);
}

// T3's errors hold on the restore session: ENOENT -> NoExiste.
static void test_a_missing_image_at_the_remembered_server_gives_no_existe(void)
{
    TwoServers rig;
    TnfsLoader loader(rig.links, rig.store, rig.clock, rig.memory, nullptr, nullptr, &rig.restore);
    TnfsFolder folder;
    const LoaderStatus status = awaitResult(loader, fetchAt(loader, "srv-a", "/nada.atr"), folder);
    TEST_ASSERT_TRUE(status.kind == Kind::Error);
    TEST_ASSERT_TRUE(status.error == LoaderError::NoExiste);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_lists_a_folder);
    RUN_TEST(test_poll_hands_over_1200_entries_without_allocating);
    RUN_TEST(test_fetches_a_whole_image_into_a_slot_with_its_sha256);
    RUN_TEST(test_job_numbers_grow_and_unknown_numbers_say_so);
    RUN_TEST(test_list_percentage_is_0_during_readdir_then_follows_the_stats);
    RUN_TEST(test_fetch_percentage_follows_the_bytes_and_stops_at_99);
    RUN_TEST(test_cancel_while_running_publishes_nothing_and_closes_on_the_server);
    RUN_TEST(test_a_new_job_replaces_the_running_one);
    RUN_TEST(test_a_cancel_just_before_publication_frees_the_slot);
    RUN_TEST(test_a_replacement_just_before_publication_frees_the_slot);
    RUN_TEST(test_a_cancel_after_publication_frees_the_slot_on_the_loader_thread);
    RUN_TEST(test_a_mute_server_gives_no_responde);
    RUN_TEST(test_a_malformed_answer_gives_no_responde);
    RUN_TEST(test_a_name_that_does_not_resolve_gives_no_encontrado_and_is_tried_again);
    RUN_TEST(test_a_silent_server_is_opened_again_by_the_next_job);
    RUN_TEST(test_a_link_that_cannot_open_gives_no_responde);
    RUN_TEST(test_a_source_without_server_gives_no_encontrado);
    RUN_TEST(test_a_missing_file_gives_no_existe_and_a_missing_folder_no_se_pudo_abrir);
    RUN_TEST(test_only_a_confirmed_enoent_gives_no_existe);
    RUN_TEST(test_no_memory_gives_no_cabe_with_the_cause);
    RUN_TEST(test_no_free_slot_gives_no_cabe);
    RUN_TEST(test_a_path_too_long_gives_no_se_pudo_abrir);
    RUN_TEST(test_start_poll_and_cancel_answer_while_a_request_is_held);
    RUN_TEST(test_the_loader_stops_with_a_job_in_progress);
    RUN_TEST(test_the_percentage_never_goes_down_after_a_reconnection);
    RUN_TEST(test_a_refused_stat_drops_the_entry_and_a_mute_one_fails);
    RUN_TEST(test_a_listing_restarted_by_a_reconnection_counts_each_entry_once);
    RUN_TEST(test_recycled_slots_are_freed_on_the_loader_thread_and_a_held_one_waits);
    RUN_TEST(test_await_returns_at_the_end_of_a_job_or_after_its_time);
    RUN_TEST(test_closing_the_loader_unmounts_each_open_session);
    RUN_TEST(test_closing_an_unused_loader_sends_nothing);
    RUN_TEST(test_closing_does_not_wait_for_a_silent_server);
    RUN_TEST(test_a_restore_asks_the_remembered_server_not_the_source);
    RUN_TEST(test_the_restore_session_follows_the_server_of_each_job);
    RUN_TEST(test_a_xex_comes_from_the_remembered_server);
    RUN_TEST(test_a_restore_without_a_usable_server_gives_no_encontrado);
    RUN_TEST(test_a_missing_image_at_the_remembered_server_gives_no_existe);
    return UNITY_END();
}
