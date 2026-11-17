#include "jsonl_log.hpp"

#include <chrono>
#include <ctime>

namespace rb::host {

using rb::sio::StepOutcome;

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
    begin("step");
    std::fprintf(file_, ",\"outcome\":\"%s\"", outcomeName(outcome));
    end();
}

void JsonlLog::event(const char* name, const char* detail)
{
    begin("event");
    std::fprintf(file_, ",\"name\":\"%s\",\"detail\":\"%s\"", name, detail);
    end();
}

void JsonlLog::summary(const rb::netsio::NetSioStats& s)
{
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
