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
// Threads: start(), poll() and cancel() run on the menu (SIO) thread. They only
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

#include "memory_image_store.hpp"
#include "rb/core/result.hpp"
#include "rb/net_tnfs/tnfs_client.hpp"
#include "rb/net_tnfs/tnfs_codec.hpp"
#include "rb/netsio/datagram_port.hpp"
#include "sha256.hpp"
#include "tnfs_folder.hpp"

namespace rb::host {

// The way to the server of one source. open() runs on the loader thread before
// the first request (it may resolve a name): NotFound when the name does not
// resolve, any other failure when the socket cannot be opened.
class TnfsLink {
public:
    virtual rb::core::Result<void> open() = 0;
    virtual rb::netsio::DatagramPort& port() = 0;

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

enum class LoaderError : uint8_t {
    NoResponde,     // no answer, a malformed answer, or the link failed
    NoEncontrado,   // the server name does not resolve, or the source has no server
    NoSePudoAbrir,  // the server refused, no memory, no free slot, path too long
};

struct LoaderStatus {
    enum class Kind : uint8_t { InProgress, Folder, Image, Error, Unknown };
    static constexpr std::size_t kCauseSize = 96;

    Kind kind = Kind::Unknown;
    uint8_t percent = 0;                    // InProgress
    std::size_t slot = 0;                   // Image: the caller now owns this slot
    uint32_t bytes = 0;                     // Image
    Sha256Digest sha256{};                  // Image
    LoaderError error = LoaderError::NoResponde;  // Error
    char cause[kCauseSize] = {};            // Error: for the host log, never for the screen
};

class TnfsLoader {
public:
    static constexpr unsigned kMaxSource = 8;  // sources 2..8 are TNFS; 0 and 1 are not

    // links[n] is source n, or null when it has no server. The store, the clock,
    // the memory and the links must outlive the loader.
    TnfsLoader(TnfsLink* const (&links)[kMaxSource + 1], MemoryImageStore& store, rb::net_tnfs::TnfsClock& clock,
               ImageMemory& memory, LoaderHooks* hooks = nullptr);
    ~TnfsLoader();
    TnfsLoader(const TnfsLoader&) = delete;
    TnfsLoader& operator=(const TnfsLoader&) = delete;

    // List the folder `path` of a source, or bring the image at `path` whole.
    // `path` is absolute UTF-8. Returns the new job's number (never 0).
    uint32_t startList(unsigned source, const char* path, std::size_t length) noexcept;
    uint32_t startFetch(unsigned source, const char* path, std::size_t length) noexcept;

    // The state of job `job`. A finished result is handed over once: a Folder is
    // swapped into `folder` (whose old content the loader frees later on its own
    // thread); an Image gives its slot to the caller.
    LoaderStatus poll(uint32_t job, TnfsFolder& folder) noexcept;

    void cancel(uint32_t job) noexcept;

private:
    enum class Kind : uint8_t { List, Fetch };
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
        TnfsFolder folder;
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
    void publish(uint32_t number, Outcome& out);
    bool cancelled(uint32_t number) const noexcept;
    void progress(uint32_t number, uint8_t percent) noexcept;

    TnfsLink* links_[kMaxSource + 1] = {};
    bool opened_[kMaxSource + 1] = {};
    std::optional<rb::net_tnfs::TnfsClient> clients_[kMaxSource + 1];
    MemoryImageStore& store_;
    ImageMemory& memory_;
    LoaderHooks* hooks_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
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
    bool doneHasSlot_ = false;
    bool discard_ = false;

    std::thread thread_;
};

}  // namespace rb::host
