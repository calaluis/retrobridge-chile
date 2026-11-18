#pragma once

// In-memory TNFS server for host tests (menu-tnfs T003, M41-5). Written from
// "The TNFS Protocol" (docs/fuentes.md) without TnfsCodec: every request is
// read and every reply is built here by hand, so a codec mistake cannot hide in
// both ends. No TNFS implementation was read.
//
// It is the client's DatagramPort: send() is a request reaching the server,
// receive() is the next reply reaching the client, delivered on the FakeClock.
// respond() answers one request directly, for T008, which puts this server
// behind a real UDP socket (there the delays are not simulated).
//
// A request that repeats the sequence number and command of the last request
// its session received is a retry (the document: byte 2 "allows the receiver to
// determine whether the datagram it just got is a retry"); log() marks it. If the
// first attempt was executed, the retry gets the same reply and is not executed
// twice, so a lost READ reply never skips a block. If it was not executed
// (silence, reject, malformed, eagain), the retry is a new attempt and the next
// programme for its command applies to it. A UMOUNT retry gets the same OK
// although the session is gone. MOUNT is not deduplicated: every MOUNT opens a
// new session, so a client that retries MOUNT may leave one behind.
//
// The tree is read-only. OPEN with any flag other than O_RDONLY, and every
// command that would change the tree, is answered EROFS and changes nothing.
// log() keeps every command received, with the flags, mode and path of OPEN.
//
// Programming, by command:
//   delay(cmd, ms)       every reply to cmd arrives ms later (a slow server)
//   silence(cmd)         the next request is not answered
//   reject(cmd, code)    the next request is answered with that status
//   malformed(cmd)       the next reply is the header alone (no status byte)
//   eagain(cmd, ms)      the next request is answered EAGAIN with that back-off
//   lateReply(cmd, ms)   the next reply arrives ms late, then normal again
//   replyWith(cmd, body) the next request is answered OK followed by `body`,
//                        without being executed (for odd but well-framed replies)
//   unreachable(n)       the next n receive() calls fail with TransportError, as
//                        when the host answers "port unreachable" (no server)
//   forgetSession(code)  now: every session and handle is gone, as after a
//                        restart; requests on an old session get `code` (0xFF)
//
// Silence models a lost request: nothing is executed, so its retry runs normally.
// Not thread-safe: in T008 the tree and every programme are set up before the
// socket thread starts, the thread calls respond(), and the test reads log()
// only after that thread has stopped.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "fakes/fake_clock.hpp"
#include "rb/core/assert.hpp"
#include "rb/netsio/datagram_port.hpp"

namespace rb::test {

class FakeTnfsServer final : public rb::netsio::DatagramPort {
public:
    using Bytes = std::vector<uint8_t>;

    struct Entry {
        uint8_t connection[2] = {0, 0};
        uint8_t sequence = 0;
        uint8_t command = 0;
        bool retry = false;     // same sequence and command as the session's last request
        bool isOpen = false;    // flags and mode are set only for OPEN
        uint16_t flags = 0;
        uint16_t mode = 0;
        std::string path;       // OPEN: the file; MOUNT: the mount location
        uint16_t version = 0;   // MOUNT only, as sent: LSB minor, MSB major
        std::string user;       // MOUNT only
        std::string password;   // MOUNT only
    };

    // Status codes, from the document's "List of Valid Return Codes".
    static constexpr uint8_t kOk = 0x00;
    static constexpr uint8_t kNoEntry = 0x02;      // ENOENT
    static constexpr uint8_t kAgain = 0x07;        // EAGAIN
    static constexpr uint8_t kNotDirectory = 0x0C; // ENOTDIR
    static constexpr uint8_t kIsDirectory = 0x0D;  // EISDIR
    static constexpr uint8_t kInvalid = 0x0E;      // EINVAL
    static constexpr uint8_t kTooManyOpen = 0x0F;  // ENFILE
    static constexpr uint8_t kReadOnly = 0x14;     // EROFS
    static constexpr uint8_t kNoSys = 0x16;        // ENOSYS
    static constexpr uint8_t kEof = 0x21;
    static constexpr uint8_t kBadHandle = 0xFF;    // "Invalid TNFS handle"

    static constexpr std::size_t kMaxRead = 512;   // largest READ this server answers

    explicit FakeTnfsServer(FakeClock& clock) : clock_(clock) { nodes_["/"] = Node{true, {}}; }

    // --- The tree ---------------------------------------------------------------
    void setMinRetryMs(uint16_t ms) { minRetryMs_ = ms; }

    // Parent directories are created as needed. Paths are absolute, "/"-separated
    // and kept as bytes (UTF-8 names stay as given).
    void addFile(const std::string& path, Bytes bytes)
    {
        const std::string p = normalise(path);
        RB_ASSERT(p.size() > 1 && wellFormed(p));
        auto existing = nodes_.find(p);
        RB_ASSERT(existing == nodes_.end() || !existing->second.isDirectory);
        addDirectory(parentOf(p));
        nodes_[p] = Node{false, std::move(bytes)};
    }
    void addDirectory(const std::string& path)
    {
        // Checks the whole chain first, so a refused call changes nothing
        // (RB_ASSERT stops the test program in every build).
        std::string p = normalise(path);
        RB_ASSERT(wellFormed(p));
        std::vector<std::string> missing;
        for (;;) {
            auto existing = nodes_.find(p);
            if (existing != nodes_.end()) {
                RB_ASSERT(existing->second.isDirectory);  // never a directory under a file
                break;
            }
            missing.push_back(p);
            p = parentOf(p);
        }
        for (const std::string& dir : missing) {
            nodes_[dir] = Node{true, {}};
        }
    }
    Bytes fileBytes(const std::string& path) const
    {
        auto it = nodes_.find(normalise(path));
        return it == nodes_.end() ? Bytes{} : it->second.bytes;
    }
    bool exists(const std::string& path) const { return nodes_.find(normalise(path)) != nodes_.end(); }

    // --- Programming ------------------------------------------------------------
    void delay(uint8_t command, uint32_t ms) { delayUs_[command] = uint64_t{ms} * 1000u; }
    void silence(uint8_t command) { programme(command, {Kind::Silence, 0, {}}); }
    void reject(uint8_t command, uint8_t code) { programme(command, {Kind::Reject, code, {}}); }
    void malformed(uint8_t command) { programme(command, {Kind::Malformed, 0, {}}); }
    void eagain(uint8_t command, uint16_t ms) { programme(command, {Kind::Eagain, ms, {}}); }
    void lateReply(uint8_t command, uint32_t ms) { programme(command, {Kind::Late, ms, {}}); }
    void replyWith(uint8_t command, Bytes body) { programme(command, {Kind::Custom, 0, std::move(body)}); }
    void unreachable(unsigned times = 1) { unreachable_ = times; }
    void forgetSession(uint8_t code = kBadHandle)
    {
        sessions_.clear();
        unknownSession_ = code;
    }

    // --- Observations -----------------------------------------------------------
    const std::vector<Entry>& log() const { return log_; }
    std::size_t openHandles() const
    {
        std::size_t count = 0;
        for (const auto& session : sessions_) {
            count += session.second.handles.size();
        }
        return count;
    }

    // --- One request, answered at once (for a real socket in T008) --------------
    std::vector<Bytes> respond(rb::core::Span<const uint8_t> request)
    {
        Outcome outcome = handle(request);
        std::vector<Bytes> replies;
        if (outcome.answered) {
            replies.push_back(std::move(outcome.reply));
        }
        return replies;
    }

    // --- DatagramPort (the client's side) ----------------------------------------
    rb::core::Result<void> send(rb::core::Span<const uint8_t> datagram) override
    {
        Outcome outcome = handle(datagram);
        if (outcome.answered) {
            outbox_.push_back({clock_.nowMicros() + outcome.delayUs, std::move(outcome.reply)});
        }
        return {};
    }

    rb::core::Result<std::size_t> receive(rb::core::Span<uint8_t> out, uint32_t timeoutUs) override
    {
        if (unreachable_ > 0) {
            --unreachable_;
            return rb::core::Failure{rb::core::ErrorCode::TransportError};
        }
        const uint64_t deadline = clock_.nowMicros() + timeoutUs;
        auto next = std::min_element(outbox_.begin(), outbox_.end(), [](const Pending& a, const Pending& b) {
            return a.atUs < b.atUs;  // first of equals: the order they were sent
        });
        if (next == outbox_.end() || next->atUs > deadline) {
            clock_.waitUntil(deadline);
            return rb::core::Failure{rb::core::ErrorCode::Timeout};
        }
        clock_.waitUntil(next->atUs);
        Bytes reply = std::move(next->bytes);
        outbox_.erase(next);
        if (reply.size() > out.size()) {
            return rb::core::Failure{rb::core::ErrorCode::TransportError};
        }
        std::copy(reply.begin(), reply.end(), out.begin());
        return reply.size();
    }

private:
    struct Node {
        bool isDirectory = false;
        Bytes bytes;
    };
    struct Handle {
        bool isDirectory = false;
        std::string path;
        std::vector<std::string> names;  // directory: entries still to give
        std::size_t next = 0;            // directory: next entry; file: read offset
    };
    struct Session {
        std::map<uint8_t, Handle> handles;
        uint8_t nextHandle = 1;
        bool hasSeen = false;  // the last request received, executed or not
        uint8_t seenSequence = 0;
        uint8_t seenCommand = 0;
        bool hasLast = false;  // the last executed request and its reply
        uint8_t lastSequence = 0;
        uint8_t lastCommand = 0;
        Bytes lastReply;
    };
    enum class Kind { Silence, Reject, Malformed, Eagain, Late, Custom };
    struct Programme {
        Kind kind;
        uint32_t value;
        Bytes body;  // Custom only
    };
    struct Outcome {
        bool answered = false;
        Bytes reply;
        uint64_t delayUs = 0;
    };
    struct Pending {
        uint64_t atUs;
        Bytes bytes;
    };

    void programme(uint8_t command, Programme p) { programmes_[command].push_back(p); }

    static std::string normalise(const std::string& path)
    {
        std::string p = path.empty() ? "/" : path;
        while (p.size() > 1 && p.back() == '/') {
            p.pop_back();
        }
        return p;
    }
    // Absolute, with no empty, "." or ".." component.
    static bool wellFormed(const std::string& p)
    {
        if (p.empty() || p[0] != '/') {
            return false;
        }
        if (p == "/") {
            return true;
        }
        std::size_t start = 1;
        for (;;) {
            const std::size_t slash = p.find('/', start);
            const std::string part = p.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (part.empty() || part == "." || part == "..") {
                return false;
            }
            if (slash == std::string::npos) {
                return true;
            }
            start = slash + 1;
        }
    }
    static std::string parentOf(const std::string& path)
    {
        const std::string p = normalise(path);
        const std::size_t slash = p.rfind('/');
        return slash == 0 || slash == std::string::npos ? "/" : p.substr(0, slash);
    }

    // Reads a NUL-terminated string at `at`; false if there is no NUL.
    static bool readString(rb::core::Span<const uint8_t> d, std::size_t& at, std::string& out)
    {
        out.clear();
        while (at < d.size()) {
            const uint8_t byte = d[at++];
            if (byte == 0x00) {
                return true;
            }
            out.push_back(static_cast<char>(byte));
        }
        return false;
    }

    static Bytes header(rb::core::Span<const uint8_t> request)
    {
        return Bytes{request[0], request[1], request[2], request[3]};
    }
    static Bytes status(rb::core::Span<const uint8_t> request, uint8_t code)
    {
        Bytes reply = header(request);
        reply.push_back(code);
        return reply;
    }
    static void putLe16(Bytes& b, uint32_t v)
    {
        b.push_back(static_cast<uint8_t>(v & 0xFF));
        b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }
    static void putLe32(Bytes& b, uint32_t v)
    {
        putLe16(b, v & 0xFFFF);
        putLe16(b, v >> 16);
    }

    static bool changesTheTree(uint8_t command)
    {
        // MKDIR, RMDIR, WRITE, UNLINK, CHMOD, RENAME.
        return command == 0x13 || command == 0x14 || command == 0x22 || command == 0x26 || command == 0x27 ||
               command == 0x28;
    }

    Outcome handle(rb::core::Span<const uint8_t> d)
    {
        Outcome outcome;
        if (d.size() < 4) {
            return outcome;  // not even a header: a server cannot answer it
        }
        const uint8_t command = d[3];
        Entry entry;
        entry.connection[0] = d[0];
        entry.connection[1] = d[1];
        entry.sequence = d[2];
        entry.command = command;
        if (command == 0x29 && d.size() >= 8) {
            entry.isOpen = true;
            entry.flags = static_cast<uint16_t>(d[4] | (d[5] << 8));
            entry.mode = static_cast<uint16_t>(d[6] | (d[7] << 8));
            std::size_t at = 8;
            readString(d, at, entry.path);
        }
        if (command == 0x00 && d.size() >= 6) {
            entry.version = static_cast<uint16_t>(d[4] | (d[5] << 8));
            std::size_t at = 6;
            if (readString(d, at, entry.path) && readString(d, at, entry.user)) {
                readString(d, at, entry.password);
            }
        }
        Session* session = command == 0x00 ? nullptr : findSession(d);
        const bool umountRetry = command == 0x01 && session == nullptr && lastUmount_.valid &&
                                 lastUmount_.connection[0] == d[0] && lastUmount_.connection[1] == d[1] &&
                                 lastUmount_.sequence == d[2];
        entry.retry = umountRetry || (session != nullptr && session->hasSeen && session->seenSequence == d[2] &&
                                      session->seenCommand == command);
        const bool cached = session != nullptr && entry.retry && session->hasLast &&
                            session->lastSequence == d[2] && session->lastCommand == command;
        if (session != nullptr) {
            session->hasSeen = true;
            session->seenSequence = d[2];
            session->seenCommand = command;
        }
        log_.push_back(entry);

        auto delayed = delayUs_.find(command);
        outcome.delayUs = delayed == delayUs_.end() ? 0 : delayed->second;

        // A retry of an executed request gets the same reply, never a second run.
        if (cached || umountRetry) {
            outcome.answered = true;
            outcome.reply = cached ? session->lastReply : lastUmount_.reply;
            return outcome;
        }

        auto& queue = programmes_[command];
        if (!queue.empty()) {
            const Programme p = queue.front();
            queue.pop_front();
            switch (p.kind) {
            case Kind::Silence:
                return outcome;
            case Kind::Reject:
                outcome.answered = true;
                outcome.reply = status(d, static_cast<uint8_t>(p.value));
                return outcome;
            case Kind::Malformed:
                outcome.answered = true;
                outcome.reply = header(d);
                return outcome;
            case Kind::Eagain:
                outcome.answered = true;
                outcome.reply = status(d, kAgain);
                putLe16(outcome.reply, p.value);
                return outcome;
            case Kind::Late:
                outcome.delayUs += uint64_t{p.value} * 1000u;
                break;
            case Kind::Custom:
                outcome.answered = true;
                outcome.reply = status(d, kOk);
                outcome.reply.insert(outcome.reply.end(), p.body.begin(), p.body.end());
                return outcome;
            }
        }
        const bool hadSession = session != nullptr;
        outcome.answered = true;
        outcome.reply = answer(d, command);
        if (command == 0x01 && hadSession) {  // the session is gone; remember its UMOUNT
            lastUmount_ = UmountReply{true, {d[0], d[1]}, d[2], outcome.reply};
        }
        if (command != 0x00 && command != 0x01) {
            if (Session* executed = findSession(d)) {
                executed->hasLast = true;
                executed->lastSequence = d[2];
                executed->lastCommand = command;
                executed->lastReply = outcome.reply;
            }
        }
        return outcome;
    }

    Session* findSession(rb::core::Span<const uint8_t> d)
    {
        auto found = sessions_.find(static_cast<uint16_t>(d[0] | (d[1] << 8)));
        return found == sessions_.end() ? nullptr : &found->second;
    }

    Bytes answer(rb::core::Span<const uint8_t> d, uint8_t command)
    {
        if (command == 0x00) {
            return mount(d);
        }
        const uint16_t id = static_cast<uint16_t>(d[0] | (d[1] << 8));
        auto found = sessions_.find(id);
        if (found == sessions_.end()) {
            return status(d, unknownSession_);
        }
        Session& session = found->second;
        switch (command) {
        case 0x01:  // UMOUNT
            sessions_.erase(found);
            return status(d, kOk);
        case 0x10:  // OPENDIR
            return openDir(d, session);
        case 0x11:  // READDIR
            return readDir(d, session);
        case 0x12:  // CLOSEDIR
        case 0x23:  // CLOSE
            return close(d, session, command == 0x12);
        case 0x24:  // STAT
            return stat(d);
        case 0x29:  // OPEN
            return open(d, session);
        case 0x21:  // READ
            return read(d, session);
        default:
            return status(d, changesTheTree(command) ? kReadOnly : kNoSys);
        }
    }

    Bytes mount(rb::core::Span<const uint8_t> d)
    {
        std::size_t at = 6;
        std::string location, user, password;
        if (d.size() < 7 || !readString(d, at, location) || !readString(d, at, user) ||
            !readString(d, at, password)) {
            Bytes reply{0x00, 0x00, d[2], d[3], kInvalid};
            return reply;
        }
        const uint16_t id = nextSession_++;
        sessions_[id] = Session{};
        Bytes reply{static_cast<uint8_t>(id & 0xFF), static_cast<uint8_t>(id >> 8), d[2], d[3], kOk};
        reply.push_back(0x02);  // version 1.2: minor, then major
        reply.push_back(0x01);
        putLe16(reply, minRetryMs_);
        return reply;
    }

    // Path argument at byte 4; empty string if missing or without NUL.
    static bool pathAt(rb::core::Span<const uint8_t> d, std::size_t at, std::string& path)
    {
        return readString(d, at, path) && !path.empty();
    }

    Bytes openDir(rb::core::Span<const uint8_t> d, Session& session)
    {
        std::string path;
        if (!pathAt(d, 4, path)) {
            return status(d, kInvalid);
        }
        path = normalise(path);
        auto node = nodes_.find(path);
        if (node == nodes_.end()) {
            return status(d, kNoEntry);
        }
        if (!node->second.isDirectory) {
            return status(d, kNotDirectory);
        }
        Handle h;
        h.isDirectory = true;
        h.path = path;
        h.names = {".", ".."};
        const std::string prefix = path == "/" ? "/" : path + "/";
        for (const auto& candidate : nodes_) {  // std::map: byte order
            const std::string& p = candidate.first;
            if (p != path && p.compare(0, prefix.size(), prefix) == 0 &&
                p.find('/', prefix.size()) == std::string::npos) {
                h.names.push_back(p.substr(prefix.size()));
            }
        }
        return newHandle(d, session, std::move(h));
    }

    Bytes newHandle(rb::core::Span<const uint8_t> d, Session& session, Handle h)
    {
        // The next free number, never 0 and never one still open.
        uint8_t number = 0;
        for (int tries = 0; tries < 255 && number == 0; ++tries) {
            const uint8_t candidate = session.nextHandle;
            session.nextHandle = static_cast<uint8_t>(candidate == 255 ? 1 : candidate + 1);
            if (session.handles.find(candidate) == session.handles.end()) {
                number = candidate;
            }
        }
        if (number == 0) {
            return status(d, kTooManyOpen);
        }
        session.handles[number] = std::move(h);
        Bytes reply = status(d, kOk);
        reply.push_back(number);
        return reply;
    }

    Handle* findHandle(rb::core::Span<const uint8_t> d, Session& session, bool directory)
    {
        if (d.size() < 5) {
            return nullptr;
        }
        auto it = session.handles.find(d[4]);
        if (it == session.handles.end() || it->second.isDirectory != directory) {
            return nullptr;
        }
        return &it->second;
    }

    Bytes readDir(rb::core::Span<const uint8_t> d, Session& session)
    {
        Handle* h = findHandle(d, session, true);
        if (h == nullptr) {
            return status(d, kBadHandle);
        }
        if (h->next >= h->names.size()) {
            return status(d, kEof);
        }
        Bytes reply = status(d, kOk);
        const std::string& name = h->names[h->next++];
        reply.insert(reply.end(), name.begin(), name.end());
        reply.push_back(0x00);
        return reply;
    }

    Bytes close(rb::core::Span<const uint8_t> d, Session& session, bool directory)
    {
        if (findHandle(d, session, directory) == nullptr) {
            return status(d, kBadHandle);
        }
        session.handles.erase(d[4]);
        return status(d, kOk);
    }

    Bytes stat(rb::core::Span<const uint8_t> d)
    {
        std::string path;
        if (!pathAt(d, 4, path)) {
            return status(d, kInvalid);
        }
        auto node = nodes_.find(normalise(path));
        if (node == nodes_.end()) {
            return status(d, kNoEntry);
        }
        Bytes reply = status(d, kOk);
        putLe16(reply, node->second.isDirectory ? 0040555 : 0100444);  // mode
        putLe16(reply, 0);                                              // uid
        putLe16(reply, 0);                                              // gid
        putLe32(reply, static_cast<uint32_t>(node->second.bytes.size()));
        putLe32(reply, 0);  // atime
        putLe32(reply, 0);  // mtime
        putLe32(reply, 0);  // ctime
        reply.push_back(0x00);  // uid string
        reply.push_back(0x00);  // gid string
        return reply;
    }

    Bytes open(rb::core::Span<const uint8_t> d, Session& session)
    {
        std::string path;
        if (d.size() < 9 || !pathAt(d, 8, path)) {
            return status(d, kInvalid);
        }
        const uint16_t flags = static_cast<uint16_t>(d[4] | (d[5] << 8));
        if (flags != 0x0001) {  // anything but O_RDONLY would write or create
            return status(d, kReadOnly);
        }
        auto node = nodes_.find(normalise(path));
        if (node == nodes_.end()) {
            return status(d, kNoEntry);
        }
        if (node->second.isDirectory) {
            return status(d, kIsDirectory);
        }
        Handle h;
        h.path = node->first;
        return newHandle(d, session, std::move(h));
    }

    Bytes read(rb::core::Span<const uint8_t> d, Session& session)
    {
        Handle* h = d.size() >= 7 ? findHandle(d, session, false) : nullptr;
        if (h == nullptr) {
            return status(d, kBadHandle);
        }
        const Bytes& file = nodes_.find(h->path)->second.bytes;  // nodes are never removed
        if (h->next >= file.size()) {
            return status(d, kEof);
        }
        const std::size_t asked = static_cast<std::size_t>(d[5] | (d[6] << 8));
        const std::size_t count = std::min({asked, kMaxRead, file.size() - h->next});
        Bytes reply = status(d, kOk);
        putLe16(reply, static_cast<uint32_t>(count));
        reply.insert(reply.end(), file.begin() + static_cast<std::ptrdiff_t>(h->next),
                     file.begin() + static_cast<std::ptrdiff_t>(h->next + count));
        h->next += count;
        return reply;
    }

    FakeClock& clock_;
    std::map<std::string, Node> nodes_;
    std::map<uint16_t, Session> sessions_;
    uint16_t nextSession_ = 0xBEEF;
    uint16_t minRetryMs_ = 1000;
    struct UmountReply {
        bool valid = false;
        uint8_t connection[2] = {0, 0};
        uint8_t sequence = 0;
        Bytes reply;
    };
    UmountReply lastUmount_;
    uint8_t unknownSession_ = kBadHandle;
    unsigned unreachable_ = 0;
    std::map<uint8_t, uint64_t> delayUs_;
    std::map<uint8_t, std::deque<Programme>> programmes_;
    std::vector<Entry> log_;
    std::deque<Pending> outbox_;
};

}  // namespace rb::test
