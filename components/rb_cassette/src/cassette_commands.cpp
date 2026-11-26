#include "rb/cassette/cassette_commands.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace rb::cassette {

namespace {

constexpr std::size_t kPathMax = 256;
constexpr const char* kUsage = "usage: cas mount <path> | status | eject | rewind";
constexpr const char* kBusy = "busy: the last tape is still closing";

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

const char* skipSpaces(const char* s)
{
    while (isSpace(*s)) {
        ++s;
    }
    return s;
}

// True if `s` is `word` followed by a space or the end; `rest` is what follows, spaces skipped.
bool takeWord(const char* s, const char* word, const char*& rest)
{
    const std::size_t length = std::strlen(word);
    if (std::strncmp(s, word, length) != 0 || (s[length] != '\0' && !isSpace(s[length]))) {
        return false;
    }
    rest = skipSpaces(s + length);
    return true;
}

void reply(rb::core::Span<char> out, const char* format, ...)
{
    if (out.size() == 0) {
        return;
    }
    va_list args;
    va_start(args, format);
    (void)std::vsnprintf(out.data(), out.size(), format, args);
    va_end(args);
}

// The file name without its folders, cut to fit `name`.
void baseName(const char* path, char (&name)[CassetteCommands::kNameMax])
{
    const char* base = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    (void)std::snprintf(name, sizeof(name), "%s", base);
}

const char* phaseName(PlayerPhase phase)
{
    switch (phase) {
    case PlayerPhase::Gap:
        return "gap";
    case PlayerPhase::Bytes:
        return "data";
    case PlayerPhase::FskWait:
        return "fsk loading";
    case PlayerPhase::FskPlay:
    case PlayerPhase::FskDrain:
        return "fsk";
    case PlayerPhase::End:
        return "end";
    }
    return "?";
}

}  // namespace

CassetteCommands::CassetteCommands(CassettePlayer& player, FskLoader& loader, TapeFiles& files,
                                   rb::core::Span<CasSegment> tableA, rb::core::Span<CasSegment> tableB) noexcept
    : player_(player), loader_(loader), files_(files), active_(tableA), spare_(tableB)
{
}

void CassetteCommands::run(const char* args, const MotorIn& motor, WaveOut& wave, rb::core::Span<char> out) noexcept
{
    (void)closeRetired();
    const char* line = skipSpaces(args == nullptr ? "" : args);
    const char* rest = nullptr;
    if (takeWord(line, "mount", rest)) {
        mount(rest, wave, out);
    } else if (takeWord(line, "status", rest) && *rest == '\0') {
        status(motor, out);
    } else if (takeWord(line, "eject", rest) && *rest == '\0') {
        eject(wave, out);
    } else if (takeWord(line, "rewind", rest) && *rest == '\0') {
        rewind(wave, out);
    } else {
        reply(out, "%s", kUsage);
    }
}

void CassetteCommands::mount(const char* args, WaveOut& wave, rb::core::Span<char> out) noexcept
{
    // The path is the rest of the line without its trailing spaces: names may have spaces.
    std::size_t length = std::strlen(args);
    while (length > 0 && isSpace(args[length - 1])) {
        --length;
    }
    if (length == 0) {
        reply(out, "usage: cas mount <path>");
        return;
    }
    if (length >= kPathMax) {
        reply(out, "path too long");
        return;
    }
    char path[kPathMax];
    std::memcpy(path, args, length);
    path[length] = '\0';

    if (!closeRetired()) {
        reply(out, "%s", kBusy);
        return;
    }
    TapeSource* fresh = files_.open(path);
    if (fresh == nullptr) {
        reply(out, "cannot open %s", path);
        return;
    }
    char name[kNameMax];
    baseName(path, name);
    const CasIndexResult index = CasIndex::build(*fresh, spare_);
    if (index.status != CasStatus::Ok) {
        files_.close(fresh);  // the mounted tape stays as it was
        switch (index.status) {
        case CasStatus::Unsupported:
            reply(out, "%s: pwm blocks are not supported", name);
            break;
        case CasStatus::Full:
            reply(out, "%s: too many blocks (max %u)", name, static_cast<unsigned>(spare_.size()));
            break;
        case CasStatus::ReadError:
            reply(out, "%s: read error", name);
            break;
        default:
            reply(out, "%s: not a valid .cas (FUJI header, cut block or baud 0)", name);
            break;
        }
        return;
    }

    unsigned data = 0;
    unsigned fsk = 0;
    for (uint32_t i = 0; i < index.count; ++i) {
        if (spare_[i].kind == SegmentKind::Data) {
            ++data;
        } else {
            fsk += spare_[i].chunks;
        }
    }
    player_.load(*fresh, rb::core::Span<const CasSegment>(spare_.data(), index.count), wave);
    const rb::core::Span<CasSegment> table = active_;
    active_ = spare_;
    spare_ = table;
    TapeSource* old = source_;
    source_ = fresh;
    count_ = index.count;
    ++generation_;
    std::memcpy(name_, name, sizeof(name_));
    if (old != nullptr) {
        retired_ = old;
        (void)closeRetired();
    }
    reply(out, "mounted %s: %u data, %u fsk", name_, data, fsk);
}

void CassetteCommands::status(const MotorIn& motor, rb::core::Span<char> out) const noexcept
{
    const char* motorText = motor.on() ? "on" : "off";
    if (source_ == nullptr) {
        reply(out, "no tape, motor %s", motorText);  // MOTOR is worth seeing before a tape (AC9)
        return;
    }
    if (player_.atEnd()) {
        reply(out, "%s: end, motor %s", name_, motorText);
        return;
    }
    reply(out, "%s: segment %u/%u, %s, motor %s%s", name_, static_cast<unsigned>(player_.segment() + 1),
          static_cast<unsigned>(count_), phaseName(player_.phase()), motorText,
          player_.blocked() ? ", run too big" : "");
}

void CassetteCommands::eject(WaveOut& wave, rb::core::Span<char> out) noexcept
{
    if (source_ == nullptr) {
        reply(out, "no tape");
        return;
    }
    if (!closeRetired()) {
        reply(out, "%s", kBusy);
        return;
    }
    player_.unload(wave);
    retired_ = source_;
    source_ = nullptr;
    count_ = 0;
    ++generation_;
    reply(out, "ejected %s", name_);
    name_[0] = '\0';
    (void)closeRetired();
}

void CassetteCommands::rewind(WaveOut& wave, rb::core::Span<char> out) noexcept
{
    if (source_ == nullptr) {
        reply(out, "no tape");
        return;
    }
    player_.rewind(wave);
    ++generation_;
    reply(out, "rewound %s", name_);
}

// The producer is done with a tape once it is no longer loading it: stopped, failed, final,
// or already loading another one (the player starts it only after the previous one stopped).
bool CassetteCommands::closeRetired() noexcept
{
    if (retired_ == nullptr) {
        return true;
    }
    if (loader_.state() == LoaderState::Loading && loader_.source() == retired_) {
        return false;
    }
    files_.close(retired_);
    retired_ = nullptr;
    return true;
}

}  // namespace rb::cassette
