#pragma once

// TNFS codec, read-only subset (menu-tnfs, ADR-012). Written from the public
// protocol document "The TNFS Protocol" (spectranet tnfs/tnfs-protocol.md,
// updated July 15, 2020; docs/fuentes.md). No implementation was read.
//
// Only nine requests can be encoded: MOUNT, UMOUNT, OPENDIR, READDIR, CLOSEDIR,
// OPEN (read-only), READ, CLOSE and STAT. There is no encoder for any request
// that changes the server, and the only way to open a file is
// encodeOpenReadOnly(), whose flags are fixed to O_RDONLY and mode to 0.
//
// Every datagram: bytes 0-1 connection id, byte 2 sequence number, byte 3
// command; in a reply, byte 4 is the status and data follows from byte 5.
// Multi-byte payload fields are little endian. The document does not state the
// byte order of the connection id, so it is kept as two opaque bytes and sent
// back exactly as the server gave them.
//
// Nothing here trusts a reply: every decoder checks lengths before reading and
// never reads outside the datagram. Pure functions, no allocation.

#include <array>
#include <cstddef>
#include <cstdint>

#include "rb/core/result.hpp"
#include "rb/core/span.hpp"

namespace rb::net_tnfs {

constexpr uint16_t kPort = 16384;  // mandatory UDP port
constexpr std::size_t kHeaderSize = 4;
constexpr std::size_t kMaxPath = 255;      // bytes of a path, without its NUL
constexpr std::size_t kMaxReadSize = 512;  // largest READ this client asks for
// Largest request: header, OPEN flags and mode, path and its NUL.
constexpr std::size_t kMaxRequest = kHeaderSize + 4 + kMaxPath + 1;
// Receive buffer for one reply. A READ reply is 7 + kMaxReadSize bytes; the
// rest is margin for a server that pads. A larger datagram is malformed.
constexpr std::size_t kMaxDatagram = 1024;

namespace cmd {
constexpr uint8_t kMount = 0x00;
constexpr uint8_t kUmount = 0x01;
constexpr uint8_t kOpenDir = 0x10;
constexpr uint8_t kReadDir = 0x11;
constexpr uint8_t kCloseDir = 0x12;
constexpr uint8_t kRead = 0x21;
constexpr uint8_t kClose = 0x23;
constexpr uint8_t kStat = 0x24;
constexpr uint8_t kOpen = 0x29;
}  // namespace cmd

// Status codes this client acts on (the document's "List of Valid Return Codes").
namespace status {
constexpr uint8_t kOk = 0x00;
constexpr uint8_t kEagain = 0x07;         // followed by a 16-bit back-off in ms
constexpr uint8_t kStale = 0x20;          // ESTALE, "Stale TNFS handle"
constexpr uint8_t kEof = 0x21;
constexpr uint8_t kInvalidHandle = 0xFF;  // "Invalid TNFS handle"
}  // namespace status

constexpr uint16_t kOpenReadOnlyFlags = 0x0001;  // O_RDONLY
constexpr uint16_t kModeTypeMask = 0170000;      // S_IFMT
constexpr uint16_t kModeDirectory = 0040000;     // S_IFDIR

// Connection id as received in the MOUNT reply (opaque, see above).
struct ConnectionId {
    std::array<uint8_t, 2> bytes{};
};

// Encoders write one request into `out` and return its length.
//   InvalidArgument: path null, empty or longer than kMaxPath.
//   BufferTooSmall:  `out` cannot hold the request.
// MOUNT is anonymous (empty user and password), protocol version 1.2, and
// always uses connection id 0 as the document requires.
rb::core::Result<std::size_t> encodeMount(uint8_t sequence, const char* mountPath,
                                          rb::core::Span<uint8_t> out) noexcept;
// UMOUNT uses sequence number 0, as in the document.
rb::core::Result<std::size_t> encodeUmount(ConnectionId connection, rb::core::Span<uint8_t> out) noexcept;
rb::core::Result<std::size_t> encodeOpenDir(ConnectionId connection, uint8_t sequence, const char* path,
                                            rb::core::Span<uint8_t> out) noexcept;
rb::core::Result<std::size_t> encodeReadDir(ConnectionId connection, uint8_t sequence, uint8_t handle,
                                            rb::core::Span<uint8_t> out) noexcept;
rb::core::Result<std::size_t> encodeCloseDir(ConnectionId connection, uint8_t sequence, uint8_t handle,
                                             rb::core::Span<uint8_t> out) noexcept;
rb::core::Result<std::size_t> encodeStat(ConnectionId connection, uint8_t sequence, const char* path,
                                         rb::core::Span<uint8_t> out) noexcept;
// Flags O_RDONLY (0x0001) and mode 0, both little endian. Not configurable.
rb::core::Result<std::size_t> encodeOpenReadOnly(ConnectionId connection, uint8_t sequence, const char* path,
                                                 rb::core::Span<uint8_t> out) noexcept;
// InvalidArgument also when size is 0 or larger than kMaxReadSize.
rb::core::Result<std::size_t> encodeRead(ConnectionId connection, uint8_t sequence, uint8_t descriptor,
                                         uint16_t size, rb::core::Span<uint8_t> out) noexcept;
rb::core::Result<std::size_t> encodeClose(ConnectionId connection, uint8_t sequence, uint8_t descriptor,
                                          rb::core::Span<uint8_t> out) noexcept;

// What a received datagram is, for the request that is waiting for it.
//   Reply:     the answer (same command and sequence number), with a status.
//   Foreign:   another command or sequence number: a late or stray datagram,
//              to be discarded (the document: "Clients should discard...").
//   Malformed: too short to be a reply, or longer than kMaxDatagram.
enum class ReplyKind : uint8_t { Reply, Foreign, Malformed };

// `body` points into the datagram (bytes after the status) and is valid only
// while that buffer is.
struct Reply {
    ReplyKind kind = ReplyKind::Malformed;
    ConnectionId connection{};
    uint8_t status = 0;
    rb::core::Span<const uint8_t> body{};
};

Reply parse(rb::core::Span<const uint8_t> datagram, uint8_t expectedCommand, uint8_t expectedSequence) noexcept;

// Body decoders. Each one requires a Reply with the status named below and
// returns InvalidArgument if the reply is not that, or if its body is too short
// or inconsistent.
struct MountInfo {
    uint8_t versionMajor = 0;
    uint8_t versionMinor = 0;
    bool hasMinRetry = false;  // the document's reply carries it; tolerate its absence
    uint16_t minRetryMs = 0;
};
rb::core::Result<MountInfo> decodeMount(const Reply& reply) noexcept;              // status kOk
rb::core::Result<uint8_t> decodeHandle(const Reply& reply) noexcept;               // OPENDIR, OPEN; kOk
// Entry name without its NUL; an empty name or a body without NUL is invalid.
rb::core::Result<rb::core::Span<const uint8_t>> decodeDirEntry(const Reply& reply) noexcept;  // kOk
// Data of a READ: count (16-bit LE) bytes; invalid if count > requested or the
// body holds fewer than count bytes.
rb::core::Result<rb::core::Span<const uint8_t>> decodeReadData(const Reply& reply,
                                                               uint16_t requested) noexcept;  // kOk

struct StatInfo {
    uint16_t mode = 0;
    uint32_t size = 0;
    bool isDirectory() const noexcept { return (mode & kModeTypeMask) == kModeDirectory; }
};
rb::core::Result<StatInfo> decodeStat(const Reply& reply) noexcept;                // kOk
rb::core::Result<uint16_t> decodeEagainDelay(const Reply& reply) noexcept;         // kEagain

}  // namespace rb::net_tnfs
