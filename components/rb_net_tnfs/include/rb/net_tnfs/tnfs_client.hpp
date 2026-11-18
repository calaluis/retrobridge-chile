#pragma once

// TNFS session client, read-only (menu-tnfs T005, ADR-012). Built on TnfsCodec
// over one DatagramPort, used by one thread (the host's loading thread). Written
// from the public protocol document (docs/fuentes.md); no implementation read.
//
// What it does for every request:
//   * a new request takes the next sequence number; a retry repeats it, so the
//     server can recognise it ("byte 2 ... whether the datagram it just got is
//     a retry") and never runs a READ twice;
//   * datagrams for another command or sequence (late copies, strays) and
//     malformed ones are discarded while it keeps waiting for its own reply;
//   * first contact (MOUNT): 3 attempts of 2000 ms, so a mute server gives
//     NoResponse after 6 s at most;
//   * after MOUNT: 3 attempts per request, each waiting the minimum retry time
//     the server announced (at most kMaxWaitUs, 5 s), or 2000 ms if it
//     announced none (or 0);
//   * EAGAIN: waits the back-off the server asks for, but never longer than one
//     attempt's wait, and tries again without using up an attempt (at most
//     kMaxEagain times per request);
//   * the server forgot the session (0xFF or 0x20, the document names no code
//     for it): one new MOUNT and the whole operation once more, from the start;
//     if that MOUNT fails or is refused, NoResponse;
//   * cancelled() is checked before each request and every kCancelCheckUs
//     during the waits of READDIR, STAT and READ. OPENDIR and OPEN are waited
//     to their end, so their handle is known and can be closed;
//   * OPENDIR and OPEN are always closed with CLOSEDIR or CLOSE, also when the
//     operation is cancelled or fails half way;
//   * a receive failure other than a timeout (for example ICMP "port
//     unreachable": nobody is listening) ends the attempt at once.
//
// Files are opened only through TnfsCodec::encodeOpenReadOnly (O_RDONLY, mode 0).
// No allocation: request and reply buffers are members.

#include <array>
#include <cstddef>
#include <cstdint>

#include "rb/core/span.hpp"
#include "rb/net_tnfs/tnfs_codec.hpp"
#include "rb/netsio/datagram_port.hpp"

namespace rb::net_tnfs {

// Monotonic time for the waits. The host passes its clock; tests a fake one.
class TnfsClock {
public:
    virtual uint64_t nowMicros() const = 0;
    virtual void sleepUntil(uint64_t micros) = 0;

protected:
    TnfsClock() = default;
    ~TnfsClock() = default;
};

enum class TnfsResult : uint8_t {
    Ok,
    NoResponse,  // no reply in time, the port failed, or the session was lost twice
    NotOpened,   // the server refused (missing path, not a file, an error status), or the sink refused
    Malformed,   // replies came but none could be used
    Cancelled,
    Rejected,    // mount() only: the server answered MOUNT with an error (see lastStatus())
};

// Asked by the client whether the caller gave up (for example, Esc).
class CancelCheck {
public:
    virtual bool cancelled() = 0;

protected:
    CancelCheck() = default;
    ~CancelCheck() = default;
};

// listDir() reports every entry the server gives, "." and ".." included, in
// the server's order. restart() means the listing starts again from the first
// entry (after a silent reconnection): forget what was received so far.
class DirVisitor : public CancelCheck {
public:
    virtual void restart() = 0;
    virtual bool entry(rb::core::Span<const uint8_t> name) = 0;  // false: stop (Cancelled)

protected:
    DirVisitor() = default;
    ~DirVisitor() = default;
};

// readWholeReadOnly() calls begin() with the size STAT gave, then data() with
// consecutive blocks. After a silent reconnection it calls begin() again and
// starts over from offset 0, so data() may write the same offsets twice.
// Progress is offset + size of each data() against the size given to begin().
class ReadSink : public CancelCheck {
public:
    virtual bool begin(uint32_t size) = 0;  // false: refuse (for example, no memory): NotOpened
    virtual void data(uint32_t offset, rb::core::Span<const uint8_t> bytes) = 0;

protected:
    ReadSink() = default;
    ~ReadSink() = default;
};

struct StatResult {
    TnfsResult result = TnfsResult::NoResponse;
    bool isDirectory = false;
    uint32_t size = 0;
};

class TnfsClient {
public:
    static constexpr uint32_t kFirstContactWaitUs = 2000000;
    static constexpr uint32_t kDefaultWaitUs = 2000000;
    // Ceiling on the wait the server announces (a uint16 in ms, up to 65 s):
    // a broken or hostile server cannot hold the client longer per attempt.
    static constexpr uint32_t kMaxWaitUs = 5000000;
    static constexpr unsigned kAttempts = 3;
    static constexpr unsigned kMaxEagain = 10;
    static constexpr unsigned kMaxEmptyReads = 3;  // OK replies with no data in a row
    static constexpr uint16_t kReadBlock = 512;
    static constexpr uint32_t kCancelCheckUs = 100000;  // waits are cut in slices this long

    TnfsClient(rb::netsio::DatagramPort& port, TnfsClock& clock) noexcept : port_(port), clock_(clock) {}
    TnfsClient(const TnfsClient&) = delete;
    TnfsClient& operator=(const TnfsClient&) = delete;

    // Mounts "/" anonymously; if already mounted, ends that session first. The
    // operations below mount on their own if needed.
    TnfsResult mount() noexcept;
    bool mounted() const noexcept { return mounted_; }
    MountInfo serverInfo() const noexcept { return info_; }
    uint8_t lastStatus() const noexcept { return lastStatus_; }  // the last error status received

    // `path` is absolute, UTF-8, 1 to kMaxPath bytes.
    TnfsResult listDir(const char* path, DirVisitor& visitor) noexcept;
    // `cancel` (may be null) is asked during the waits.
    StatResult stat(const char* path, CancelCheck* cancel = nullptr) noexcept;
    TnfsResult readWholeReadOnly(const char* path, ReadSink& sink) noexcept;

    // Sends UMOUNT if mounted and forgets the session, whatever the answer.
    void umount() noexcept;

private:
    enum class Exchange : uint8_t { Reply, NoResponse, Malformed, Cancelled };
    enum class Op : uint8_t { Ok, NoResponse, NotOpened, Malformed, Cancelled, SessionLost };

    // Sends request_[0..length) (already encoded with `sequence`) and waits for
    // its reply, with the attempts, discards and EAGAIN rules above. On Reply,
    // `reply` points into received_ and is valid until the next exchange.
    // `cancel` (may be null) is asked during the waits.
    Exchange exchange(std::size_t length, uint8_t command, uint8_t sequence, bool firstContact, Reply& reply,
                      CancelCheck* cancel) noexcept;
    // Waits `us`, asking `cancel` every kCancelCheckUs. False if cancelled.
    bool sleepFor(uint64_t us, CancelCheck* cancel) noexcept;
    static Op failed(Exchange exchange) noexcept;  // the Op for an exchange that gave no Reply

    Op listOnce(const char* path, DirVisitor& visitor) noexcept;
    Op statOnce(const char* path, StatResult& out, CancelCheck* cancel) noexcept;
    Op readOnce(const char* path, ReadSink& sink) noexcept;
    void closeQuietly(uint8_t command, uint8_t handle) noexcept;

    TnfsResult ensureMounted() noexcept;
    bool reconnect() noexcept;  // a new MOUNT after the session was lost
    static bool sessionLost(uint8_t status) noexcept
    {
        return status == status::kInvalidHandle || status == status::kStale;
    }
    static TnfsResult toResult(Op op) noexcept;
    uint32_t waitUs(bool firstContact) const noexcept;
    uint8_t nextSequence() noexcept { return ++sequence_; }

    rb::netsio::DatagramPort& port_;
    TnfsClock& clock_;
    ConnectionId connection_{};
    MountInfo info_{};
    bool mounted_ = false;
    uint8_t sequence_ = 0;
    uint8_t lastStatus_ = 0;
    std::array<uint8_t, kMaxRequest> request_{};
    std::array<uint8_t, kMaxDatagram> received_{};
};

}  // namespace rb::net_tnfs
