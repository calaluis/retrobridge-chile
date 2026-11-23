#pragma once

// TnfsLoader of retrobridge_host (menu-tnfs T011, M41-1, M41-6): one thread
// that lists TNFS folders and brings whole images into the MemoryImageStore, so
// no answer to a key of the Atari waits for the network.
//
// One job at a time, with a number. start() of a new job cancels the one in
// progress; the result of a cancelled or replaced job is never published, and
// its slot and its listing are freed on the loader thread. A result is handed
// over once, by poll(); after that the number is unknown.
//
// Threads: start(), poll(), cancel(), await() and recycle() run on the menu (SIO) thread. They only
// take the mutex for a few copies: they never wait for the network and never
// allocate (ADR-011); the mutex is never held during a TNFS request. Opening a
// link, the TNFS requests, the allocations, the SHA-256 and the release of a
// slot happen on the loader thread.
//
// Percentage (plan, T011 Interfaces): 0..99 while a job is in progress, never
// going down. Fetch: 100 * bytes read / size. List: 0 while READDIR runs, then
// 100 * entries asked with STAT / entries to ask. Entries starting with '.' are
// dropped without STAT.

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "rb/menu/memory_image_store.hpp"
#include "rb/menu/menu_loader.hpp"
#include "rb/core/result.hpp"
#include "rb/net_tnfs/tnfs_client.hpp"
#include "rb/net_tnfs/tnfs_codec.hpp"
#include "rb/netsio/datagram_port.hpp"
#include "rb/menu/sha256.hpp"
#include "rb/menu/tnfs_folder.hpp"

namespace rb::host {

// The way to the server of one source. open() runs on the loader thread before
// the first request (it may resolve a name): NotFound when the name does not
// resolve, any other failure when the socket cannot be opened.
class TnfsLink {
public:
    virtual rb::core::Result<void> open() = 0;
    virtual rb::netsio::DatagramPort& port() = 0;
    // The server becomes `server` (menu-editar-servidor); the next open() uses it.
    // Called on the loader thread only, between jobs.
    virtual void rename(const char* server) noexcept = 0;

protected:
    TnfsLink() = default;
    ~TnfsLink() = default;
};

// Memory for a whole image. false when it cannot be had: the loader reports
// NoSePudoAbrir, with the size asked in the cause for the host log.
class ImageMemory {
public:
    virtual bool allocate(ImageBuffer& out, uint32_t size) = 0;

protected:
    ImageMemory() = default;
    ~ImageMemory() = default;
};

// The heap, through ImageBuffer::allocate (new (std::nothrow), ADR-004).
class HeapImageMemory final : public ImageMemory {
public:
    bool allocate(ImageBuffer& out, uint32_t size) override;
};

// Test seam, on the loader thread: beforePublish() when a job has its result
// and just before it is published; afterPublish() once it is (or once it was
// discarded). A test forces a cancel into either window.
class LoaderHooks {
public:
    virtual void beforePublish(uint32_t job) = 0;
    virtual void afterPublish(uint32_t job) = 0;

protected:
    LoaderHooks() = default;
    ~LoaderHooks() = default;
};

class TnfsLoader final : public MenuLoader {
public:
    static constexpr unsigned kMaxSource = 8;  // sources 2..8 are TNFS; 0 and 1 are not

    // links[n] is source n, or null when it has no server. The store, the clock,
    // the memory and the links must outlive the loader. `imagesFolder` (menu-xex) is the
    // --images folder a local .xex is read from (startXex with source 1); it must outlive
    // the loader too. nullptr: a local .xex cannot be opened.
    TnfsLoader(TnfsLink* const (&links)[kMaxSource + 1], MemoryImageStore& store, rb::net_tnfs::TnfsClock& clock,
               ImageMemory& memory, LoaderHooks* hooks = nullptr, const char* imagesFolder = nullptr);
    ~TnfsLoader();
    TnfsLoader(const TnfsLoader&) = delete;
    TnfsLoader& operator=(const TnfsLoader&) = delete;

    // List the folder `path` of a source, or bring the image at `path` whole.
    // `path` is absolute UTF-8. Returns the new job's number (never 0).
    // false if a listing block could not be reserved (main does not start then).
    bool ready() const noexcept { return doneFolder_.ready() && workFolder_.ready(); }
    uint32_t startList(unsigned source, const char* path, std::size_t length) noexcept override;
    uint32_t startFetch(unsigned source, const char* path, std::size_t length) noexcept override;
    uint32_t startXex(unsigned source, const char* path, std::size_t length) noexcept override;

    // The state of job `job`. A finished result is handed over once: a Folder is
    // swapped into `folder` (whose old content the loader frees later on its own
    // thread); an Image gives its slot to the caller.
    LoaderStatus poll(uint32_t job, TnfsFolder& folder) noexcept override;

    void cancel(uint32_t job) noexcept override;

    // Waits on the menu thread, without allocating, until job `job` is done, is
    // replaced or cancelled, or `micros` passed.
    void await(uint32_t job, uint32_t micros) noexcept override;

    // M41-20: marks `slot` to be freed on the loader thread, which tries before
    // every job and whenever it wakes; a slot still open or attached keeps its
    // mark for the next wake. A free slot drops it.
    void recycle(std::size_t slot) noexcept override;

    // menu-editar-servidor: applied on the loader thread before its next job.
    void changeServer(unsigned source, const char* server) noexcept override;
    static constexpr std::size_t kMaxServer = 32;  // MenuSources::kMaxName

private:
    enum class Kind : uint8_t { List, Fetch, Xex };
    enum class State : uint8_t { None, Pending, Running, Done };
    struct Job {
        Kind kind = Kind::List;
        unsigned source = 0;
        uint32_t number = 0;
        std::size_t length = 0;
        bool tooLong = false;
        char path[rb::net_tnfs::kMaxPath + 1] = {};
    };
    struct Outcome {
        LoaderStatus status;
        bool hasSlot = false;
    };
    class Cancel;
    class Lister;
    class Sink;

    uint32_t start(Kind kind, unsigned source, const char* path, std::size_t length) noexcept;
    void run();
    void execute(const Job& job, Outcome& out);
    void list(const Job& job, rb::net_tnfs::TnfsClient& client, Outcome& out);
    void fetch(const Job& job, rb::net_tnfs::TnfsClient& client, Outcome& out);
    void readLocal(const Job& job, Outcome& out);
    void adopt(const Job& job, ImageBuffer&& bytes, Outcome& out);
    void publish(uint32_t number, Outcome& out);
    void freeRecycled();
    void applyRenames();
    bool cancelled(uint32_t number) const noexcept;
    void progress(uint32_t number, uint8_t percent) noexcept;

    TnfsLink* links_[kMaxSource + 1] = {};
    bool opened_[kMaxSource + 1] = {};
    std::optional<rb::net_tnfs::TnfsClient> clients_[kMaxSource + 1];
    MemoryImageStore& store_;
    ImageMemory& memory_;
    LoaderHooks* hooks_;
    const char* imagesFolder_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable done_;  // a job was published or dropped (await)
    bool recycle_[MemoryImageStore::kSlots] = {};  // slots the menu gave back (M41-20)
    bool recycleReady_ = false;                     // a new mark since the loader last looked
    char rename_[kMaxSource + 1][kMaxServer + 1] = {};  // new server names (changeServer)
    bool renamed_[kMaxSource + 1] = {};
    bool renameReady_ = false;
    bool stop_ = false;
    uint32_t counter_ = 0;
    uint32_t current_ = 0;  // the job the menu waits for; 0: none
    uint32_t running_ = 0;  // the job the loader thread works on; 0: none
    State state_ = State::None;
    bool pendingReady_ = false;
    Job pending_;
    uint8_t percent_ = 0;
    // The published result of current_ (state_ Done), or one to discard.
    LoaderStatus doneStatus_;
    TnfsFolder doneFolder_;
    // The listing being built; only the loader thread touches it (publish() swaps it
    // with doneFolder_ under the lock). Its block, like doneFolder_'s and the menu's,
    // is reserved once: the three blocks change hands, never get freed or reserved.
    TnfsFolder workFolder_;
    bool doneHasSlot_ = false;
    bool discard_ = false;

    std::thread thread_;
};

}  // namespace rb::host
