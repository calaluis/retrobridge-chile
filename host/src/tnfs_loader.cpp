#include "tnfs_loader.hpp"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace rb::host {

using rb::core::ErrorCode;
using rb::core::Span;
using rb::net_tnfs::StatResult;
using rb::net_tnfs::TnfsClient;
using rb::net_tnfs::TnfsResult;

static_assert(kTnfsMaxPath == rb::net_tnfs::kMaxPath, "the menu keeps paths the client can send");

static_assert(TnfsFolder::kMaxPath == rb::net_tnfs::kMaxPath, "a kept entry can always be opened");

namespace {

void setError(LoaderStatus& status, LoaderError error, const char* format, ...)
{
    status.kind = LoaderStatus::Kind::Error;
    status.error = error;
    va_list args;
    va_start(args, format);
    std::vsnprintf(status.cause, sizeof status.cause, format, args);
    va_end(args);
}

const char* resultName(TnfsResult result) noexcept
{
    switch (result) {
    case TnfsResult::Ok:
        return "ok";
    case TnfsResult::NoResponse:
        return "sin respuesta";
    case TnfsResult::NotOpened:
        return "rechazado";
    case TnfsResult::Malformed:
        return "respuesta mal formada";
    case TnfsResult::Cancelled:
        return "cancelado";
    case TnfsResult::Rejected:
        return "MOUNT rechazado";
    }
    return "?";
}

// NoResponse and Malformed: the server did not answer usably. NotOpened and a
// refused MOUNT: it answered, and said no.
LoaderError errorOf(TnfsResult result) noexcept
{
    return (result == TnfsResult::NotOpened || result == TnfsResult::Rejected) ? LoaderError::NoSePudoAbrir
                                                                                : LoaderError::NoResponde;
}

uint8_t percentOf(uint64_t done, uint64_t total) noexcept
{
    if (total == 0) {
        return 0;
    }
    const uint64_t percent = 100u * done / total;
    return static_cast<uint8_t>(std::min<uint64_t>(percent, 99));
}

std::string join(const char* folder, const std::string& name)
{
    std::string path(folder);
    if (path.empty() || path.back() != '/') {
        path += '/';
    }
    return path + name;
}

}  // namespace

bool HeapImageMemory::allocate(ImageBuffer& out, uint32_t size)
{
    out = ImageBuffer::allocate(size);
    return out.valid();
}

// ---------------------------------------------------------------------------
// What the TNFS client asks while a job runs.

class TnfsLoader::Cancel final : public rb::net_tnfs::CancelCheck {
public:
    Cancel(const TnfsLoader& loader, uint32_t job) noexcept : loader_(loader), job_(job) {}
    bool cancelled() override { return loader_.cancelled(job_); }

private:
    const TnfsLoader& loader_;
    uint32_t job_;
};

class TnfsLoader::Lister final : public rb::net_tnfs::DirVisitor {
public:
    Lister(const TnfsLoader& loader, uint32_t job) noexcept : loader_(loader), job_(job) {}
    bool cancelled() override { return loader_.cancelled(job_); }
    void restart() override { names.clear(); }
    bool entry(Span<const uint8_t> name) override
    {
        // "." and "..", and every other name starting with '.', are dropped here,
        // without STAT (plan, T011 Interfaces).
        if (name.size() != 0 && name[0] != '.') {
            names.emplace_back(reinterpret_cast<const char*>(name.data()), name.size());
        }
        return true;
    }

    std::vector<std::string> names;

private:
    const TnfsLoader& loader_;
    uint32_t job_;
};

class TnfsLoader::Sink final : public rb::net_tnfs::ReadSink {
public:
    Sink(TnfsLoader& loader, uint32_t job, ImageMemory& memory) noexcept : loader_(loader), job_(job), memory_(memory)
    {
    }
    bool cancelled() override { return loader_.cancelled(job_); }

    bool begin(uint32_t size) override
    {
        // Called again, from offset 0, after a silent reconnection.
        if (!buffer.valid() || buffer.size() != size) {
            buffer = ImageBuffer{};
            if (!memory_.allocate(buffer, size) || !buffer.valid() || buffer.size() != size) {
                buffer = ImageBuffer{};
                noMemory = true;
                asked = size;
                return false;
            }
        }
        loader_.progress(job_, 0);
        return true;
    }

    void data(uint32_t offset, Span<const uint8_t> bytes) override
    {
        const uint64_t end = uint64_t{offset} + bytes.size();
        if (end <= buffer.size() && bytes.size() != 0) {  // the client never passes more than STAT said
            std::memcpy(buffer.data() + offset, bytes.data(), bytes.size());
        }
        loader_.progress(job_, percentOf(end, buffer.size()));
    }

    ImageBuffer buffer;
    bool noMemory = false;
    uint32_t asked = 0;

private:
    TnfsLoader& loader_;
    uint32_t job_;
    ImageMemory& memory_;
};

// ---------------------------------------------------------------------------

TnfsLoader::TnfsLoader(TnfsLink* const (&links)[kMaxSource + 1], MemoryImageStore& store,
                       rb::net_tnfs::TnfsClock& clock, ImageMemory& memory, LoaderHooks* hooks)
    : store_(store), memory_(memory), hooks_(hooks)
{
    for (unsigned n = 2; n <= kMaxSource; ++n) {
        links_[n] = links[n];
        if (links_[n] != nullptr) {
            clients_[n].emplace(links_[n]->port(), clock);
        }
    }
    thread_ = std::thread([this] { run(); });
}

TnfsLoader::~TnfsLoader()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;  // the running job sees cancelled() and ends
    }
    wake_.notify_all();
    thread_.join();
    // End every session the loader opened (review of PR #44) with one UMOUNT
    // each, without waiting: a silent server cannot hold the shutdown. Nothing
    // goes out for a client that never mounted. The thread is gone, so this
    // thread owns the clients now.
    for (auto& client : clients_) {
        if (client.has_value()) {
            client->umountWithoutWaiting();
        }
    }
    if (doneHasSlot_) {
        (void)store_.release(doneStatus_.slot);
    }
}

uint32_t TnfsLoader::startList(unsigned source, const char* path, std::size_t length) noexcept
{
    return start(Kind::List, source, path, length);
}

uint32_t TnfsLoader::startFetch(unsigned source, const char* path, std::size_t length) noexcept
{
    return start(Kind::Fetch, source, path, length);
}

uint32_t TnfsLoader::start(Kind kind, unsigned source, const char* path, std::size_t length) noexcept
{
    uint32_t number = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (++counter_ == 0) {
            ++counter_;  // 0 means "no job"
        }
        number = counter_;
        Job& job = pending_;
        job.kind = kind;
        job.source = source;
        job.number = number;
        job.length = length;
        job.tooLong = path == nullptr || length == 0 || length > rb::net_tnfs::kMaxPath;
        if (!job.tooLong) {
            std::memcpy(job.path, path, length);
        }
        job.path[job.tooLong ? 0 : length] = '\0';
        if (state_ == State::Done) {
            discard_ = true;  // the previous result was never taken
        }
        current_ = number;  // a running job is cancelled by this
        state_ = State::Pending;
        pendingReady_ = true;
        percent_ = 0;
    }
    wake_.notify_all();
    return number;
}

LoaderStatus TnfsLoader::poll(uint32_t job, TnfsFolder& folder) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    LoaderStatus status;
    if (job == 0 || job != current_) {
        return status;  // Unknown
    }
    if (state_ == State::Pending || state_ == State::Running) {
        status.kind = LoaderStatus::Kind::InProgress;
        status.percent = percent_;
        return status;
    }
    // Done: hand the result over, once.
    status = doneStatus_;
    if (status.kind == LoaderStatus::Kind::Folder) {
        folder.swap(doneFolder_);  // blocks change hands: nothing allocated, no entry visited
    }
    doneHasSlot_ = false;  // an Image slot now belongs to the caller
    current_ = 0;
    state_ = State::None;
    return status;
}

void TnfsLoader::cancel(uint32_t job) noexcept
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (job == 0 || job != current_) {
            return;
        }
        if (state_ == State::Pending) {
            pendingReady_ = false;
        } else if (state_ == State::Done) {
            discard_ = true;
        }
        current_ = 0;
        state_ = State::None;
    }
    wake_.notify_all();
    done_.notify_all();
}

void TnfsLoader::await(uint32_t job, uint32_t micros) noexcept
{
    std::unique_lock<std::mutex> lock(mutex_);
    done_.wait_for(lock, std::chrono::microseconds(micros),
                   [this, job] { return stop_ || job != current_ || state_ == State::Done; });
}

void TnfsLoader::recycle(std::size_t slot) noexcept
{
    if (slot >= MemoryImageStore::kSlots) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        recycle_[slot] = true;
        recycleReady_ = true;
    }
    wake_.notify_all();
}

// Loader thread: frees every marked slot it can. One still open or attached
// keeps its mark for the next wake; a free one drops it.
void TnfsLoader::freeRecycled()
{
    bool marked[MemoryImageStore::kSlots] = {};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t i = 0; i < MemoryImageStore::kSlots; ++i) {
            marked[i] = recycle_[i];
            recycle_[i] = false;
        }
        recycleReady_ = false;
    }
    for (std::size_t i = 0; i < MemoryImageStore::kSlots; ++i) {
        if (marked[i] && !store_.release(i) && store_.inUse(i)) {
            std::lock_guard<std::mutex> lock(mutex_);
            recycle_[i] = true;  // still held: try again at the next wake
        }
    }
}

bool TnfsLoader::cancelled(uint32_t job) const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stop_ || current_ != job;
}

void TnfsLoader::progress(uint32_t job, uint8_t percent) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (job == current_ && job == running_ && percent > percent_) {
        percent_ = percent;  // never goes down, also after a restart from offset 0
    }
}

void TnfsLoader::run()
{
    for (;;) {
        Job job;
        bool haveJob = false;
        bool freeSlot = false;
        std::size_t slot = 0;
        bool stopping = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || pendingReady_ || discard_ || recycleReady_; });
            if (discard_) {
                freeSlot = doneHasSlot_;
                slot = doneStatus_.slot;
                doneHasSlot_ = false;
                doneFolder_.clear();
                discard_ = false;
            }
            stopping = stop_;
            if (!stopping && pendingReady_) {
                job = pending_;
                pendingReady_ = false;
                running_ = job.number;
                if (current_ == job.number) {
                    state_ = State::Running;
                }
                haveJob = true;
            }
        }
        if (freeSlot) {
            (void)store_.release(slot);
        }
        if (stopping) {
            return;
        }
        freeRecycled();  // before every job, so its image finds the slots given back
        if (haveJob) {
            Outcome out;
            execute(job, out);
            publish(job.number, out);
        }
    }
}

void TnfsLoader::execute(const Job& job, Outcome& out)
{
    LoaderStatus& status = out.status;
    const unsigned source = job.source;
    if (source < 2 || source > kMaxSource || links_[source] == nullptr) {
        setError(status, LoaderError::NoEncontrado, "fuente %u sin servidor", source);
        return;
    }
    if (job.tooLong) {
        setError(status, LoaderError::NoSePudoAbrir, "ruta de %llu bytes (de 1 a %u)",
                 static_cast<unsigned long long>(job.length), static_cast<unsigned>(rb::net_tnfs::kMaxPath));
        return;
    }
    if (!opened_[source]) {
        const rb::core::Result<void> opened = links_[source]->open();
        if (!opened.ok()) {
            if (opened.error() == ErrorCode::NotFound) {
                setError(status, LoaderError::NoEncontrado, "fuente %u: el nombre del servidor no se resuelve", source);
            } else {
                setError(status, LoaderError::NoResponde, "fuente %u: no se pudo abrir el enlace (error %u)", source,
                         static_cast<unsigned>(opened.error()));
            }
            return;
        }
        opened_[source] = true;
    }
    TnfsClient& client = *clients_[source];
    if (job.kind == Kind::List) {
        list(job, client, out);
    } else {
        fetch(job, client, out);
    }
}

void TnfsLoader::list(const Job& job, TnfsClient& client, Outcome& out)
{
    LoaderStatus& status = out.status;
    Lister lister(*this, job.number);
    const TnfsResult listed = client.listDir(job.path, lister);
    if (listed == TnfsResult::Cancelled) {
        return;
    }
    if (listed != TnfsResult::Ok) {
        setError(status, errorOf(listed), "listar %s: %s (estado 0x%02X)", job.path, resultName(listed),
                 client.lastStatus());
        return;
    }
    // Phase 2: STAT every entry kept, to know folders from files.
    Cancel cancel(*this, job.number);
    std::vector<TnfsListing> entries;
    entries.reserve(lister.names.size());
    for (std::size_t i = 0; i < lister.names.size(); ++i) {
        const std::string& name = lister.names[i];
        const std::string path = join(job.path, name);
        if (path.size() <= rb::net_tnfs::kMaxPath) {  // a longer one could never be opened
            const StatResult st = client.stat(path.c_str(), &cancel);
            if (st.result == TnfsResult::Ok) {
                entries.push_back(TnfsListing{name, st.isDirectory});
            } else if (st.result == TnfsResult::Cancelled) {
                return;
            } else if (st.result != TnfsResult::NotOpened) {  // NotOpened: the entry went away
                setError(status, errorOf(st.result), "STAT %s: %s", path.c_str(), resultName(st.result));
                return;
            }
        }
        progress(job.number, percentOf(i + 1, lister.names.size()));
    }
    if (!workFolder_.fill(job.path, std::move(entries))) {
        setError(status, LoaderError::NoSePudoAbrir, "sin memoria para el listado de %s", job.path);
        return;
    }
    status.kind = LoaderStatus::Kind::Folder;
}

void TnfsLoader::fetch(const Job& job, TnfsClient& client, Outcome& out)
{
    LoaderStatus& status = out.status;
    Sink sink(*this, job.number, memory_);
    const TnfsResult read = client.readWholeReadOnly(job.path, sink);
    if (sink.noMemory) {
        setError(status, LoaderError::NoSePudoAbrir, "sin memoria: la reserva de %lu bytes para %s fallo",
                 static_cast<unsigned long>(sink.asked), job.path);
        return;
    }
    if (read == TnfsResult::Cancelled) {
        return;
    }
    if (read != TnfsResult::Ok) {
        setError(status, errorOf(read), "traer %s: %s (estado 0x%02X)", job.path, resultName(read),
                 client.lastStatus());
        return;
    }
    status.bytes = sink.buffer.size();
    status.sha256 = sha256(Span<const uint8_t>(sink.buffer.data(), sink.buffer.size()));
    const rb::core::Result<std::size_t> slot = store_.adopt(std::move(sink.buffer));
    if (!slot.ok()) {
        setError(status, LoaderError::NoSePudoAbrir, "sin hueco libre para %s (%lu bytes)", job.path,
                 static_cast<unsigned long>(status.bytes));
        return;
    }
    status.kind = LoaderStatus::Kind::Image;
    status.slot = slot.value();
    out.hasSlot = true;
}

void TnfsLoader::publish(uint32_t job, Outcome& out)
{
    if (hooks_ != nullptr) {
        hooks_->beforePublish(job);
    }
    bool freeSlot = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = 0;
        if (stop_ || current_ != job) {
            freeSlot = out.hasSlot;  // cancelled or replaced: never published
        } else {
            doneStatus_ = out.status;
            if (out.status.kind == LoaderStatus::Kind::Folder) {
                doneFolder_.swap(workFolder_);  // the old block comes back to be filled next time
            }
            doneHasSlot_ = out.hasSlot;
            state_ = State::Done;
        }
    }
    done_.notify_all();
    if (freeSlot) {
        (void)store_.release(out.status.slot);
    }
    if (hooks_ != nullptr) {
        hooks_->afterPublish(job);
    }
}

}  // namespace rb::host
