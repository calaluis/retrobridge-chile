#include "rb/net_tnfs/tnfs_codec.hpp"

#include <cstring>

namespace rb::net_tnfs {

using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;
using rb::core::Span;

namespace {

constexpr uint8_t kVersionMinor = 0x02;  // this client speaks protocol 1.2
constexpr uint8_t kVersionMajor = 0x01;

// Length of a path without its NUL, or 0 if it is null, empty or too long.
// Reads at most kMaxPath + 1 bytes, so an oversized input is never scanned to its end.
std::size_t pathLength(const char* path) noexcept
{
    if (path == nullptr) {
        return 0;
    }
    std::size_t length = 0;
    while (length <= kMaxPath && path[length] != '\0') {
        ++length;
    }
    return length <= kMaxPath ? length : 0;
}

// Writes the four header bytes. Precondition: out.size() >= kHeaderSize.
void putHeader(Span<uint8_t> out, ConnectionId connection, uint8_t sequence, uint8_t command) noexcept
{
    out[0] = connection.bytes[0];
    out[1] = connection.bytes[1];
    out[2] = sequence;
    out[3] = command;
}

// Header followed by `extra` bytes.
Result<std::size_t> withBytes(ConnectionId connection, uint8_t sequence, uint8_t command,
                              std::initializer_list<uint8_t> extra, Span<uint8_t> out) noexcept
{
    const std::size_t length = kHeaderSize + extra.size();
    if (out.size() < length) {
        return Failure{ErrorCode::BufferTooSmall};
    }
    putHeader(out, connection, sequence, command);
    std::size_t at = kHeaderSize;
    for (uint8_t byte : extra) {
        out[at++] = byte;
    }
    return length;
}

// Header, `prefix` bytes, then the path and its NUL.
Result<std::size_t> withPath(ConnectionId connection, uint8_t sequence, uint8_t command,
                             std::initializer_list<uint8_t> prefix, const char* path, Span<uint8_t> out) noexcept
{
    const std::size_t pathBytes = pathLength(path);
    if (pathBytes == 0) {
        return Failure{ErrorCode::InvalidArgument};
    }
    const std::size_t length = kHeaderSize + prefix.size() + pathBytes + 1;
    if (out.size() < length) {
        return Failure{ErrorCode::BufferTooSmall};
    }
    putHeader(out, connection, sequence, command);
    std::size_t at = kHeaderSize;
    for (uint8_t byte : prefix) {
        out[at++] = byte;
    }
    std::memcpy(out.data() + at, path, pathBytes);
    out[at + pathBytes] = 0x00;
    return length;
}

uint16_t le16(Span<const uint8_t> bytes, std::size_t at) noexcept
{
    return static_cast<uint16_t>(bytes[at] | (bytes[at + 1] << 8));
}

uint32_t le32(Span<const uint8_t> bytes, std::size_t at) noexcept
{
    return static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<uint32_t>(bytes[at + 2]) << 16) | (static_cast<uint32_t>(bytes[at + 3]) << 24);
}

// The reply is a Reply with the given status and at least `minBody` body bytes.
bool isReply(const Reply& reply, uint8_t wanted, std::size_t minBody) noexcept
{
    return reply.kind == ReplyKind::Reply && reply.status == wanted && reply.body.size() >= minBody;
}

}  // namespace

Result<std::size_t> encodeMount(uint8_t sequence, const char* mountPath, Span<uint8_t> out) noexcept
{
    // Version, mount point, then empty user id and password (anonymous).
    Result<std::size_t> written =
        withPath(ConnectionId{}, sequence, cmd::kMount, {kVersionMinor, kVersionMajor}, mountPath, out);
    if (!written.ok()) {
        return written;
    }
    const std::size_t length = written.value() + 2;
    if (out.size() < length) {
        return Failure{ErrorCode::BufferTooSmall};
    }
    out[length - 2] = 0x00;
    out[length - 1] = 0x00;
    return length;
}

Result<std::size_t> encodeUmount(ConnectionId connection, Span<uint8_t> out) noexcept
{
    return withBytes(connection, 0x00, cmd::kUmount, {}, out);
}

Result<std::size_t> encodeOpenDir(ConnectionId connection, uint8_t sequence, const char* path,
                                  Span<uint8_t> out) noexcept
{
    return withPath(connection, sequence, cmd::kOpenDir, {}, path, out);
}

Result<std::size_t> encodeReadDir(ConnectionId connection, uint8_t sequence, uint8_t handle,
                                  Span<uint8_t> out) noexcept
{
    return withBytes(connection, sequence, cmd::kReadDir, {handle}, out);
}

Result<std::size_t> encodeCloseDir(ConnectionId connection, uint8_t sequence, uint8_t handle,
                                   Span<uint8_t> out) noexcept
{
    return withBytes(connection, sequence, cmd::kCloseDir, {handle}, out);
}

Result<std::size_t> encodeStat(ConnectionId connection, uint8_t sequence, const char* path,
                               Span<uint8_t> out) noexcept
{
    return withPath(connection, sequence, cmd::kStat, {}, path, out);
}

Result<std::size_t> encodeOpenReadOnly(ConnectionId connection, uint8_t sequence, const char* path,
                                       Span<uint8_t> out) noexcept
{
    constexpr uint8_t flagsLow = kOpenReadOnlyFlags & 0xFF;
    constexpr uint8_t flagsHigh = kOpenReadOnlyFlags >> 8;
    return withPath(connection, sequence, cmd::kOpen, {flagsLow, flagsHigh, 0x00, 0x00}, path, out);
}

Result<std::size_t> encodeRead(ConnectionId connection, uint8_t sequence, uint8_t descriptor, uint16_t size,
                               Span<uint8_t> out) noexcept
{
    if (size == 0 || size > kMaxReadSize) {
        return Failure{ErrorCode::InvalidArgument};
    }
    return withBytes(connection, sequence, cmd::kRead,
                     {descriptor, static_cast<uint8_t>(size & 0xFF), static_cast<uint8_t>(size >> 8)}, out);
}

Result<std::size_t> encodeClose(ConnectionId connection, uint8_t sequence, uint8_t descriptor,
                                Span<uint8_t> out) noexcept
{
    return withBytes(connection, sequence, cmd::kClose, {descriptor}, out);
}

Reply parse(Span<const uint8_t> datagram, uint8_t expectedCommand, uint8_t expectedSequence) noexcept
{
    Reply reply{};
    if (datagram.size() < kHeaderSize || datagram.size() > kMaxDatagram) {
        return reply;  // Malformed
    }
    if (datagram[2] != expectedSequence || datagram[3] != expectedCommand) {
        reply.kind = ReplyKind::Foreign;
        return reply;
    }
    if (datagram.size() < kHeaderSize + 1) {
        return reply;  // ours, but without a status byte
    }
    reply.kind = ReplyKind::Reply;
    reply.connection.bytes[0] = datagram[0];
    reply.connection.bytes[1] = datagram[1];
    reply.status = datagram[4];
    reply.body = datagram.subspan(kHeaderSize + 1, datagram.size() - kHeaderSize - 1);
    return reply;
}

Result<MountInfo> decodeMount(const Reply& reply) noexcept
{
    if (!isReply(reply, status::kOk, 2)) {
        return Failure{ErrorCode::InvalidArgument};
    }
    MountInfo info{};
    info.versionMinor = reply.body[0];
    info.versionMajor = reply.body[1];
    if (reply.body.size() >= 4) {
        info.hasMinRetry = true;
        info.minRetryMs = le16(reply.body, 2);
    }
    return info;
}

Result<uint8_t> decodeHandle(const Reply& reply) noexcept
{
    if (!isReply(reply, status::kOk, 1)) {
        return Failure{ErrorCode::InvalidArgument};
    }
    return reply.body[0];
}

Result<Span<const uint8_t>> decodeDirEntry(const Reply& reply) noexcept
{
    if (!isReply(reply, status::kOk, 1)) {
        return Failure{ErrorCode::InvalidArgument};
    }
    const void* nul = std::memchr(reply.body.data(), 0x00, reply.body.size());
    if (nul == nullptr) {
        return Failure{ErrorCode::InvalidArgument};
    }
    const std::size_t length = static_cast<std::size_t>(static_cast<const uint8_t*>(nul) - reply.body.data());
    if (length == 0) {
        return Failure{ErrorCode::InvalidArgument};
    }
    return reply.body.first(length);
}

Result<Span<const uint8_t>> decodeReadData(const Reply& reply, uint16_t requested) noexcept
{
    if (!isReply(reply, status::kOk, 2)) {
        return Failure{ErrorCode::InvalidArgument};
    }
    const uint16_t count = le16(reply.body, 0);
    if (count > requested || reply.body.size() - 2 < count) {
        return Failure{ErrorCode::InvalidArgument};
    }
    return reply.body.subspan(2, count);
}

Result<StatInfo> decodeStat(const Reply& reply) noexcept
{
    // mode (2), uid (2), gid (2), size (4); the times and names that follow are not needed.
    if (!isReply(reply, status::kOk, 10)) {
        return Failure{ErrorCode::InvalidArgument};
    }
    StatInfo info{};
    info.mode = le16(reply.body, 0);
    info.size = le32(reply.body, 6);
    return info;
}

Result<uint16_t> decodeEagainDelay(const Reply& reply) noexcept
{
    if (!isReply(reply, status::kEagain, 2)) {
        return Failure{ErrorCode::InvalidArgument};
    }
    return le16(reply.body, 0);
}

}  // namespace rb::net_tnfs
