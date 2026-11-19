#include "jsonl_log.hpp"

#include <chrono>
#include <ctime>

namespace rb::host {

using rb::sio::StepOutcome;

// Length (2 to 4) of the well-formed UTF-8 sequence that starts at `p`, or 0:
// no overlong forms, no surrogates, nothing above U+10FFFF (RFC 3629, table 3-7
// of The Unicode Standard). Reads at most up to the first byte that fails, so
// it never passes the string's NUL.
std::size_t utf8SequenceLength(const unsigned char* p) noexcept
{
    const unsigned char c = p[0];
    std::size_t length = 0;
    unsigned char low = 0x80;
    unsigned char high = 0xBF;
    if (c >= 0xC2 && c <= 0xDF) {
        length = 2;
    } else if (c >= 0xE0 && c <= 0xEF) {
        length = 3;
        low = c == 0xE0 ? 0xA0 : 0x80;
        high = c == 0xED ? 0x9F : 0xBF;
    } else if (c >= 0xF0 && c <= 0xF4) {
        length = 4;
        low = c == 0xF0 ? 0x90 : 0x80;
        high = c == 0xF4 ? 0x8F : 0xBF;
    } else {
        return 0;
    }
    if (p[1] < low || p[1] > high) {
        return 0;
    }
    for (std::size_t i = 2; i < length; ++i) {
        if (p[i] < 0x80 || p[i] > 0xBF) {
            return 0;
        }
    }
    return length;
}

const char* messageName(uint8_t id) noexcept
{
    // Names from FEP-003 (docs/fuentes.md); only those a disk drive meets.
    switch (id) {
    case 0x01: return "DATA_BYTE";
    case 0x02: return "DATA_BLOCK";
    case 0x09: return "DATA_BYTE_SYNC";
    case 0x10: return "COMMAND_OFF";
    case 0x11: return "COMMAND_ON";
    case 0x18: return "COMMAND_OFF_SYNC";
    case 0x80: return "SPEED_CHANGE";
    case 0x81: return "SYNC_RESPONSE";
    case 0xC0: return "DEVICE_DISCONNECTED";
    case 0xC1: return "DEVICE_CONNECTED";
    case 0xC2: return "PING_REQUEST";
    case 0xC3: return "PING_RESPONSE";
    case 0xC4: return "ALIVE_REQUEST";
    case 0xC5: return "ALIVE_RESPONSE";
    case 0xC6: return "CREDIT_STATUS";
    case 0xC7: return "CREDIT_UPDATE";
    case 0xFE: return "WARM_RESET";
    case 0xFF: return "COLD_RESET";
    default: return "OTHER";
    }
}

const char* outcomeName(StepOutcome outcome) noexcept
{
    switch (outcome) {
    case StepOutcome::Idle: return "Idle";
    case StepOutcome::MailboxApplied: return "MailboxApplied";
    case StepOutcome::Ignored: return "Ignored";
    case StepOutcome::Nacked: return "Nacked";
    case StepOutcome::Completed: return "Completed";
    case StepOutcome::TransportFailed: return "TransportFailed";
    }
    return "Unknown";
}

void JsonlLog::begin(const char* kind)
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count() % 1000000;
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S", &utc);
    std::fprintf(file_, "{\"utc\":\"%s.%06ldZ\",\"mono_us\":%llu,\"kind\":\"%s\"", stamp, static_cast<long>(micros),
                 static_cast<unsigned long long>(clock_.nowMicros()), kind);
}

void JsonlLog::end()
{
    std::fputs("}\n", file_);
    std::fflush(file_);
}

void JsonlLog::onDatagram(bool outbound, rb::core::Span<const uint8_t> datagram)
{
    std::lock_guard<std::mutex> lock(mutex_);
    begin("datagram");
    std::fprintf(file_, ",\"dir\":\"%s\",\"type\":\"%s\",\"len\":%u,\"hex\":\"", outbound ? "out" : "in",
                 datagram.empty() ? "EMPTY" : messageName(datagram[0]), static_cast<unsigned>(datagram.size()));
    for (std::size_t i = 0; i < datagram.size(); ++i) {
        std::fprintf(file_, i == 0 ? "%02X" : " %02X", datagram[i]);
    }
    std::fputc('"', file_);
    end();
}

void JsonlLog::step(StepOutcome outcome)
{
    std::lock_guard<std::mutex> lock(mutex_);
    begin("step");
    std::fprintf(file_, ",\"outcome\":\"%s\"", outcomeName(outcome));
    end();
}

void JsonlLog::event(const char* name, const char* detail)
{
    std::lock_guard<std::mutex> lock(mutex_);
    begin("event");
    std::fprintf(file_, ",\"name\":\"%s\",\"detail\":\"", name);
    // The detail may carry a file name of the images folder (Issue #38) or a
    // TNFS path (UTF-8, review of PR #44): escape what JSON does not allow raw
    // in a string. A well-formed UTF-8 sequence goes out as it is; any other
    // byte >= 0x80 (an ANSI path) as Latin-1, so the line stays UTF-8.
    for (const char* p = detail; *p != '\0'; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        const std::size_t length = utf8SequenceLength(reinterpret_cast<const unsigned char*>(p));
        if (length > 1) {
            std::fwrite(p, 1, length, file_);
            p += length - 1;
        } else if (c == '"' || c == '\\') {
            std::fputc('\\', file_);
            std::fputc(c, file_);
        } else if (c < 0x20 || c >= 0x80) {
            std::fprintf(file_, "\\u%04X", c);
        } else {
            std::fputc(c, file_);
        }
    }
    std::fputc('"', file_);
    end();
}

void JsonlLog::summary(const rb::netsio::NetSioStats& s)
{
    std::lock_guard<std::mutex> lock(mutex_);
    begin("summary");
    std::fprintf(file_,
                 ",\"datagramsIn\":%u,\"datagramsOut\":%u,\"malformed\":%u,\"ignoredMessages\":%u,"
                 "\"framesDelivered\":%u,\"framesDiscarded\":%u,\"validSyncResponses\":%u,"
                 "\"emptySyncResponses\":%u,\"creditRequests\":%u,\"aliveRequests\":%u,\"aliveResponses\":%u,"
                 "\"resets\":%u,\"trailerGaps\":%u,\"lastBaud\":%u",
                 s.datagramsIn, s.datagramsOut, s.malformed, s.ignoredMessages, s.framesDelivered, s.framesDiscarded,
                 s.validSyncResponses, s.emptySyncResponses, s.creditRequests, s.aliveRequests, s.aliveResponses,
                 s.resets, s.trailerGaps, s.lastBaud);
    end();
}

}  // namespace rb::host
