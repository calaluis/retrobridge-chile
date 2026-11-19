#include "tnfs_link.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

#include "rb/net_tnfs/tnfs_codec.hpp"

namespace rb::host {

namespace {

// Names from "The TNFS Protocol" (docs/fuentes.md); only the nine requests the
// client can encode are named, any other byte is shown as a number.
const char* commandName(uint8_t command) noexcept
{
    namespace cmd = rb::net_tnfs::cmd;
    switch (command) {
    case cmd::kMount: return "MOUNT";
    case cmd::kUmount: return "UMOUNT";
    case cmd::kOpenDir: return "OPENDIR";
    case cmd::kReadDir: return "READDIR";
    case cmd::kCloseDir: return "CLOSEDIR";
    case cmd::kRead: return "READ";
    case cmd::kClose: return "CLOSE";
    case cmd::kStat: return "STAT";
    case cmd::kOpen: return "OPEN";
    default: return nullptr;
    }
}

// Appends at most what fits; `used` never passes size - 1.
void append(char* out, std::size_t size, std::size_t& used, const char* format, ...) noexcept
#if defined(__GNUC__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;

void append(char* out, std::size_t size, std::size_t& used, const char* format, ...) noexcept
{
    if (used + 1 >= size) {
        return;
    }
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(out + used, size - used, format, args);
    va_end(args);
    if (written > 0) {
        used = std::min(size - 1, used + static_cast<std::size_t>(written));
    }
}

// " path=<bytes up to the NUL or the end of the datagram>".
void appendPath(char* out, std::size_t size, std::size_t& used, rb::core::Span<const uint8_t> request,
                std::size_t at) noexcept
{
    std::size_t end = at;
    while (end < request.size() && request[end] != 0) {
        ++end;
    }
    append(out, size, used, " path=%.*s", static_cast<int>(end - at), reinterpret_cast<const char*>(request.data() + at));
}

}  // namespace

void describeTnfsRequest(rb::core::Span<const uint8_t> request, char* out, std::size_t size) noexcept
{
    namespace cmd = rb::net_tnfs::cmd;
    if (size == 0) {
        return;
    }
    out[0] = '\0';
    std::size_t used = 0;
    if (request.size() < rb::net_tnfs::kHeaderSize) {
        append(out, size, used, "short request (%u bytes)", static_cast<unsigned>(request.size()));
        return;
    }
    const uint8_t command = request[3];
    const char* name = commandName(command);
    if (name != nullptr) {
        append(out, size, used, "cmd=%s seq=%u", name, static_cast<unsigned>(request[2]));
    } else {
        append(out, size, used, "cmd=0x%02X seq=%u", static_cast<unsigned>(command), static_cast<unsigned>(request[2]));
    }
    const std::size_t body = rb::net_tnfs::kHeaderSize;
    switch (command) {
    case cmd::kOpen:
        // Flags and mode, little endian, then the path.
        if (request.size() < body + 4) {
            append(out, size, used, " (cut short)");
            return;
        }
        append(out, size, used, " flags=0x%04X mode=0x%04X",
               static_cast<unsigned>(request[body] | (request[body + 1] << 8)),
               static_cast<unsigned>(request[body + 2] | (request[body + 3] << 8)));
        appendPath(out, size, used, request, body + 4);
        break;
    case cmd::kMount:
        // Protocol version (2 bytes), then the mount path.
        if (request.size() < body + 2) {
            append(out, size, used, " (cut short)");
            return;
        }
        appendPath(out, size, used, request, body + 2);
        break;
    case cmd::kOpenDir:
    case cmd::kStat:
        appendPath(out, size, used, request, body);
        break;
    default:
        break;
    }
}

rb::core::Result<void> TnfsTracePort::send(rb::core::Span<const uint8_t> datagram)
{
    char request[rb::net_tnfs::kMaxPath + 96];
    describeTnfsRequest(datagram, request, sizeof request);
    char detail[sizeof request + 16];
    std::snprintf(detail, sizeof detail, "source=%u %s", source_, request);
    events_.menuEvent("tnfs_request", detail);
    return inner_.send(datagram);
}

}  // namespace rb::host
