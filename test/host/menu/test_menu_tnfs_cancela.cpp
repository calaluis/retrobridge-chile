// Cancelling a network restore (menu-tnfs-persistente T7; AC6, plan §2.3 and §3): the user's
// choice wins. Mounting or assigning another image on a drive still CARGANDO, or emptying it,
// drops its restore and cancels the loader job; a result that comes later changes neither the
// drive nor the drive memory. A user's job takes the loader and the restore comes back after it.
//
// Deterministic: a StepLoader stands in for the TnfsLoader. Nothing runs on another thread; the
// test decides when each job has its result, and the loader keeps a result even for a cancelled
// job (the worst case: the real loader drops it), so a late poll would be seen.

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "menu_tnfs_rig.hpp"

namespace {

using rb::host::DriveMemory;
using rb::host::LoaderStatus;
using Origin = rb::host::DriveMemory::Origin;

// The fake's own bookkeeping (map, strings) allocates; the rig counts what key() allocates, so
// the fake pauses the count while it records (the controller's allocations stay counted).
struct Uncounted {
    Uncounted() : was_(t_counting) { t_counting = false; }
    ~Uncounted() { t_counting = was_; }
    bool was_;
};

struct StepLoader final : rb::host::MenuLoader {
    explicit StepLoader(MemoryImageStore& store) : store_(store) {}

    struct Job {
        bool restore = false;
        bool list = false;
        std::string server;
        std::string path;
        Bytes image;      // empty: still in progress
        bool cancelled = false;
        int pollsAfterCancel = 0;
    };

    uint32_t startList(unsigned, const char* path, std::size_t length) noexcept override
    {
        return add(false, true, "", path, length);
    }
    uint32_t startFetch(unsigned, const char* path, std::size_t length) noexcept override
    {
        return add(false, false, "", path, length);
    }
    uint32_t startXex(unsigned, const char* path, std::size_t length) noexcept override
    {
        return add(false, false, "", path, length);
    }
    uint32_t startFetchAt(const char* server, const char* path, std::size_t length) noexcept override
    {
        return add(true, false, server, path, length);
    }
    uint32_t startXexAt(const char* server, const char* path, std::size_t length) noexcept override
    {
        return add(true, false, server, path, length);
    }
    LoaderStatus poll(uint32_t job, rb::host::TnfsFolder&) noexcept override
    {
        LoaderStatus status;
        auto found = jobs.find(job);
        if (found == jobs.end()) {
            return status;  // Unknown
        }
        Job& j = found->second;
        if (j.cancelled) {
            ++j.pollsAfterCancel;  // a late poll: the controller must never make one
        }
        if (j.list) {
            status.kind = LoaderStatus::Kind::Folder;  // an empty folder
            return status;
        }
        if (j.image.empty()) {
            status.kind = LoaderStatus::Kind::InProgress;
            return status;
        }
        auto slot = store_.adopt(rb::host::ImageBuffer::copyOf(Span<const uint8_t>(j.image.data(), j.image.size())));
        TEST_ASSERT_TRUE(slot.ok());
        j.image.clear();
        status.kind = LoaderStatus::Kind::Image;
        status.slot = slot.value();
        status.bytes = 92176;
        return status;
    }
    void cancel(uint32_t job) noexcept override
    {
        Uncounted quiet;
        auto found = jobs.find(job);
        if (found != jobs.end()) {
            found->second.cancelled = true;
        }
        cancels.push_back(job);
    }
    void await(uint32_t, uint32_t) noexcept override {}
    void recycle(std::size_t slot) noexcept override
    {
        Uncounted quiet;
        recycled.push_back(slot);
        (void)store_.release(slot);
    }
    void changeServer(unsigned, const char*) noexcept override {}

    uint32_t lastRestore() const
    {
        uint32_t last = 0;
        for (const auto& [id, job] : jobs) {
            if (job.restore) {
                last = id;
            }
        }
        return last;
    }
    int latePolls() const
    {
        int total = 0;
        for (const auto& [id, job] : jobs) {
            total += job.pollsAfterCancel;
        }
        return total;
    }

    std::map<uint32_t, Job> jobs;
    std::vector<uint32_t> cancels;
    std::vector<std::size_t> recycled;

private:
    uint32_t add(bool restore, bool list, const char* server, const char* path, std::size_t length)
    {
        Uncounted quiet;
        Job job;
        job.restore = restore;
        job.list = list;
        job.server = server;
        job.path.assign(path, length);
        jobs[next_] = job;
        return next_++;
    }
    MemoryImageStore& store_;
    uint32_t next_ = 1;
};

void writeFolder()  // the local folder: A DISCO.ATR
{
    const std::string dir = RB_TEST_MENU_DIR;
    std::error_code ignored;
    std::filesystem::create_directories(dir, ignored);
    std::ofstream out(dir + "/DISCO.ATR", std::ios::binary | std::ios::trunc);
    const Bytes disk = atr(720, 1);
    out.write(reinterpret_cast<const char*>(disk.data()), static_cast<std::streamsize>(disk.size()));
}

// The rig with the StepLoader in place of the TnfsLoader, powered up with `memory`. The restore
// of the network drive has started when this returns.
struct StepRig {
    Rig rig;
    StepLoader loader{rig.repository->memory()};

    explicit StepRig(const DriveMemory& memory)
    {
        rig.controller.emplace(rig.catalog, rig.sources, *rig.repository, rig.mailbox, &rig.events, &loader);
        rig.controller->onColdReset();
        rig.controller->service();
        rig.step();
        rig.controller->beginRestore();
        rig.controller->restore(memory);
        for (int i = 0; i < 20 && rig.controller->restoring(); ++i) {
            rig.step();
        }
        TEST_ASSERT_FALSE(rig.controller->restoring());
        rig.step();  // the restore asks the loader
        TEST_ASSERT_NOT_EQUAL(0, loader.lastRestore());
    }

    // The loader has the image of job `job` now, even if it was cancelled.
    void deliver(uint32_t job) { loader.jobs[job].image = atr(720, 9); }
    void steps(int n = 5)
    {
        for (int i = 0; i < n; ++i) {
            rig.step();
        }
    }
    // The saver of the host: each new version is written and confirmed (otherwise the main
    // screen waits on GUARDANDO). `saved` keeps the last version written.
    void save()
    {
        if (rig.controller->memoryChanged()) {
            DriveMemory written;
            const uint32_t version = rig.controller->snapshot(written);
            rig.controller->saveResult(version, true, 0);
            saved = std::make_unique<DriveMemory>(written);
            ++saves;
        }
    }
    MenuReply settle(uint8_t code)
    {
        MenuReply reply = rig.press(code);
        for (int i = 0; i < 200 && reply.action() == rb::host::kMenuActionWait; ++i) {
            save();
            reply = rig.press(kPoll);
        }
        TEST_ASSERT_NOT_EQUAL(rb::host::kMenuActionWait, reply.action());
        return reply;
    }
    std::unique_ptr<DriveMemory> saved;
    int saves = 0;

    std::unique_ptr<DriveMemory> snapshot()
    {
        auto out = std::make_unique<DriveMemory>();
        (void)rig.controller->snapshot(*out);
        return out;
    }
};

std::unique_ptr<DriveMemory> logoOn(unsigned drive)
{
    auto memory = std::make_unique<DriveMemory>();
    TEST_ASSERT_TRUE(memory->setNet(drive, Origin::NetAtr, "pulgarcito", kLogoPath, "ATARI LOGO.ATR"));
    return memory;
}

bool contains(const std::vector<uint32_t>& list, uint32_t value)
{
    for (uint32_t v : list) {
        if (v == value) {
            return true;
        }
    }
    return false;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// Positive control: left alone, the delivered image is mounted on D2 and nothing is cancelled.
void test_a_restore_left_alone_mounts_its_image()
{
    StepRig s(*logoOn(2));
    const uint32_t job = s.loader.lastRestore();
    s.deliver(job);
    s.steps();
    TEST_ASSERT_EQUAL_STRING_LEN("*tnfs*", s.rig.mounted(2), 6);
    TEST_ASSERT_FALSE(contains(s.loader.cancels, job));
    TEST_ASSERT_TRUE(s.snapshot()->origin(2) == Origin::NetAtr);
}

// AC6: the user mounts another image on D2 while it loads; the late image changes nothing.
void test_mounting_another_image_on_a_loading_drive_cancels_its_restore()
{
    StepRig s(*logoOn(2));
    const uint32_t job = s.loader.lastRestore();
    assert_line(s.rig.press(kRedraw), driveLine(2), "D2: CARGANDO ATARI LOGO.ATR");
    s.rig.press(K1);
    s.rig.press(KA);  // DISCO.ATR
    s.settle(K2);
    s.rig.step();  // the user's mount is applied
    TEST_ASSERT_EQUAL_STRING("DISCO.ATR", s.rig.mounted(2));
    TEST_ASSERT_TRUE(contains(s.loader.cancels, job));
    TEST_ASSERT_TRUE(s.rig.events.has("D2 net dropped: user"));
    const auto chosen = s.snapshot();
    TEST_ASSERT_TRUE(chosen->origin(2) == Origin::Local);
    TEST_ASSERT_EQUAL_STRING("DISCO.ATR", chosen->name(2));
    TEST_ASSERT_EQUAL_INT(1, s.saves);  // the user's change is a new version, saved
    TEST_ASSERT_TRUE(s.saved->origin(2) == Origin::Local);

    s.deliver(job);  // the download ends after all
    s.steps();
    TEST_ASSERT_EQUAL_STRING("DISCO.ATR", s.rig.mounted(2));
    TEST_ASSERT_EQUAL_INT(0, s.loader.latePolls());
    TEST_ASSERT_FALSE(s.rig.controller->memoryChanged());  // no new version after the late image
    const auto after = s.snapshot();
    TEST_ASSERT_TRUE(after->origin(2) == Origin::Local);
    TEST_ASSERT_EQUAL_STRING("DISCO.ATR", after->name(2));
    TEST_ASSERT_EQUAL_size_t(0, slotsInUse(s.rig.store()));  // no slot taken by the late image
    TEST_ASSERT_EQUAL_UINT(1, static_cast<unsigned>(s.loader.jobs.size()));  // never asked again
}

// AC6: the user empties D2 while it loads (U, 2): empty at once, saved, and the late image
// changes nothing.
void test_emptying_a_loading_drive_cancels_its_restore_and_saves_it_empty()
{
    StepRig s(*logoOn(2));
    const uint32_t job = s.loader.lastRestore();
    s.rig.press(KU);
    const MenuReply emptied = s.rig.press(K2);
    assert_line(emptied, driveLine(2), "D2 SIN DISCO");
    TEST_ASSERT_TRUE(contains(s.loader.cancels, job));
    TEST_ASSERT_TRUE(s.rig.controller->memoryChanged());
    TEST_ASSERT_TRUE(s.snapshot()->origin(2) == Origin::Empty);

    s.deliver(job);
    s.steps();
    TEST_ASSERT_EQUAL_STRING("", s.rig.mounted(2));
    TEST_ASSERT_EQUAL_INT(0, s.loader.latePolls());
    TEST_ASSERT_TRUE(s.snapshot()->origin(2) == Origin::Empty);
    TEST_ASSERT_EQUAL_size_t(0, slotsInUse(s.rig.store()));
}

// Emptying a drive still waiting its turn (D3 Pending while D2 loads) leaves D2's job alone.
void test_emptying_a_waiting_drive_leaves_the_loading_one_alone()
{
    auto memory = logoOn(2);
    TEST_ASSERT_TRUE(memory->setNet(3, Origin::NetAtr, "pulgarcito", kLogoPath, "ATARI LOGO.ATR"));
    StepRig s(*memory);
    const uint32_t job = s.loader.lastRestore();
    s.rig.press(KU);
    s.rig.press(K3);
    TEST_ASSERT_FALSE(contains(s.loader.cancels, job));
    TEST_ASSERT_TRUE(s.snapshot()->origin(3) == Origin::Empty);
    s.deliver(job);
    s.steps();
    TEST_ASSERT_EQUAL_STRING_LEN("*tnfs*", s.rig.mounted(2), 6);
    TEST_ASSERT_EQUAL_STRING("", s.rig.mounted(3));
    TEST_ASSERT_EQUAL_UINT(1, static_cast<unsigned>(s.loader.jobs.size()));  // D3 never asked
}

// AC6 on D1: the user assigns a local image to D1 while D1 loads; A boots the user's image.
void test_assigning_d1_while_it_loads_cancels_its_restore()
{
    StepRig s(*logoOn(1));
    const uint32_t job = s.loader.lastRestore();
    s.rig.press(K1);
    s.rig.press(KA);
    s.settle(K1);
    TEST_ASSERT_EQUAL_STRING("DISCO.ATR", s.rig.controller->driveName(1));
    TEST_ASSERT_TRUE(contains(s.loader.cancels, job));
    s.deliver(job);
    s.steps();
    TEST_ASSERT_EQUAL_STRING("DISCO.ATR", s.rig.controller->driveName(1));
    TEST_ASSERT_EQUAL_INT(0, s.loader.latePolls());
    TEST_ASSERT_EQUAL_UINT8(rb::host::kMenuActionBoot, s.rig.press(KA).action());
    s.rig.step();
    TEST_ASSERT_EQUAL_STRING("DISCO.ATR", s.rig.mounted(1));
    TEST_ASSERT_TRUE(s.snapshot()->origin(1) == Origin::Local);
}

// Plan §2.3: a user's job takes the loader; the restore goes back to the queue and, once the
// user is done, is asked for again and mounted.
void test_a_users_job_pauses_the_restore_and_it_comes_back_after()
{
    StepRig s(*logoOn(2));
    const uint32_t first = s.loader.lastRestore();
    s.settle(K2);  // the user opens source 2: a listing job
    TEST_ASSERT_TRUE(contains(s.loader.cancels, first));
    TEST_ASSERT_TRUE(s.rig.events.has("D2 net paused: user job"));
    s.rig.press(KESC);  // back to the main screen; the loader is free
    s.steps();
    const uint32_t again = s.loader.lastRestore();
    TEST_ASSERT_NOT_EQUAL(first, again);
    TEST_ASSERT_EQUAL_STRING("pulgarcito", s.loader.jobs[again].server.c_str());
    s.deliver(again);
    s.steps();
    TEST_ASSERT_EQUAL_STRING_LEN("*tnfs*", s.rig.mounted(2), 6);
    TEST_ASSERT_EQUAL_INT(0, s.loader.latePolls());
}

int main(void)
{
    writeFolder();
    UNITY_BEGIN();
    RUN_TEST(test_a_restore_left_alone_mounts_its_image);
    RUN_TEST(test_mounting_another_image_on_a_loading_drive_cancels_its_restore);
    RUN_TEST(test_emptying_a_loading_drive_cancels_its_restore_and_saves_it_empty);
    RUN_TEST(test_emptying_a_waiting_drive_leaves_the_loading_one_alone);
    RUN_TEST(test_assigning_d1_while_it_loads_cancels_its_restore);
    RUN_TEST(test_a_users_job_pauses_the_restore_and_it_comes_back_after);
    return UNITY_END();
}
