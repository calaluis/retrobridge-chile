#include "rb/menu/menu_sources.hpp"

#include <cerrno>
#include <cstring>

namespace rb::host {

namespace {

// A line read() counts as a source: not blank and not a '#' comment once the
// spaces around it are gone.
bool isSourceLine(const char* line) noexcept
{
    while (*line == ' ' || *line == '\t') {
        ++line;
    }
    return *line != '\0' && *line != '\n' && *line != '\r' && *line != '#';
}

bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }
bool isLetter(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

bool validIpv4(const char* text, std::size_t length) noexcept
{
    unsigned parts = 0;
    std::size_t i = 0;
    while (i <= length) {
        std::size_t digits = 0;
        unsigned value = 0;
        while (i < length && isDigit(text[i])) {
            value = value * 10 + static_cast<unsigned>(text[i] - '0');
            ++digits;
            ++i;
        }
        if (digits == 0 || digits > 3 || value > 255 || (digits > 1 && text[i - digits] == '0')) {
            return false;
        }
        ++parts;
        if (i == length) {
            break;
        }
        ++i;  // the '.'
        if (i == length) {
            return false;  // a trailing dot
        }
    }
    return parts == 4;
}

int defaultRename(const char* from, const char* to) { return std::rename(from, to); }

bool exists(const char* path) noexcept
{
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    std::fclose(file);
    return true;
}

bool withSuffix(char (&out)[512], const char* path, const char* suffix) noexcept
{
    return std::snprintf(out, sizeof out, "%s%s", path, suffix) < static_cast<int>(sizeof out);
}

}  // namespace

int (*sourcesFileRename)(const char* from, const char* to) = defaultRename;

namespace {

// `tmp` (complete) becomes `path`. FAT on ESP-IDF (f_rename) and std::rename on Windows refuse a
// target that exists, so the old file is first renamed to `backup`: at every moment one whole
// copy has a name. If the last rename fails, the old file goes back. The PC takes the same path,
// so the host tests run what the FujiNet runs.
bool replaceFile(const char* tmp, const char* path, const char* backup) noexcept
{
    const bool hadOld = exists(path);
    if (hadOld) {
        std::remove(backup);  // a stale one from an old cut
        if (sourcesFileRename(path, backup) != 0) {
            return false;  // nothing changed
        }
    }
    if (sourcesFileRename(tmp, path) == 0) {
        if (hadOld) {
            std::remove(backup);
        }
        return true;
    }
    if (hadOld) {
        (void)sourcesFileRename(backup, path);  // the old file keeps its name
    }
    return false;
}

}  // namespace

const char* MenuSources::name(unsigned number) const noexcept
{
    if (number < 2 || number - 2 >= count_) {
        return nullptr;
    }
    return names_[number - 2].data();
}

bool MenuSources::read(std::FILE* file, char* error, std::size_t errorSize) noexcept
{
    count_ = 0;
    char line[256];
    unsigned number = 0;
    while (std::fgets(line, sizeof line, file) != nullptr) {
        ++number;
        std::size_t length = std::strlen(line);
        if (length == sizeof line - 1 && line[length - 1] != '\n' && !std::feof(file)) {
            std::snprintf(error, errorSize, "line %u is longer than %u characters", number,
                          static_cast<unsigned>(kMaxName));
            count_ = 0;
            return false;
        }
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r' || line[length - 1] == ' ' ||
                              line[length - 1] == '\t')) {
            line[--length] = '\0';
        }
        const char* text = line;
        while (*text == ' ' || *text == '\t') {
            ++text;
            --length;
        }
        if (length == 0 || text[0] == '#') {
            continue;
        }
        if (length > kMaxName) {
            std::snprintf(error, errorSize, "line %u is longer than %u characters", number,
                          static_cast<unsigned>(kMaxName));
            count_ = 0;
            return false;
        }
        for (std::size_t i = 0; i < length; ++i) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if (c < 0x20 || c > 0x7E) {
                std::snprintf(error, errorSize, "line %u has a character outside printable ASCII", number);
                count_ = 0;
                return false;
            }
        }
        if (count_ == kMaxSources) {
            std::snprintf(error, errorSize, "more than %u sources (line %u)", static_cast<unsigned>(kMaxSources),
                          number);
            count_ = 0;
            return false;
        }
        std::memcpy(names_[count_].data(), text, length);
        names_[count_][length] = '\0';
        ++count_;
    }
    if (std::ferror(file) != 0) {
        std::snprintf(error, errorSize, "read error");
        count_ = 0;
        return false;
    }
    return true;
}

unsigned MenuSources::set(unsigned number, const char* name) noexcept
{
    if (number < 2) {
        return 0;
    }
    std::size_t index = number - 2;
    if (index >= count_) {
        if (count_ == kMaxSources) {
            return 0;
        }
        index = count_++;
    }
    const std::size_t length = std::strlen(name) < kMaxName ? std::strlen(name) : kMaxName;
    std::memcpy(names_[index].data(), name, length);
    names_[index][length] = '\0';
    return static_cast<unsigned>(index + 2);
}

bool MenuSources::remove(unsigned number) noexcept
{
    if (number < 2 || number - 2 >= count_) {
        return false;
    }
    for (std::size_t i = number - 2; i + 1 < count_; ++i) {
        names_[i] = names_[i + 1];
    }
    names_[--count_][0] = '\0';
    return true;
}

bool MenuSources::validServer(const char* text) noexcept
{
    const std::size_t length = std::strlen(text);
    if (length == 0 || length > kMaxName) {
        return false;
    }
    bool numeric = true;
    for (std::size_t i = 0; i < length; ++i) {
        numeric = numeric && (isDigit(text[i]) || text[i] == '.');
    }
    if (numeric) {
        return validIpv4(text, length);
    }
    std::size_t label = 0;
    for (std::size_t i = 0; i <= length; ++i) {
        const char c = i < length ? text[i] : '.';
        if (c == '.') {
            if (label == 0 || label > 63 || text[i - 1] == '-') {
                return false;  // an empty label, a long one, or one ending in '-'
            }
            label = 0;
        } else if (isLetter(c) || isDigit(c) || c == '-') {
            if (label == 0 && c == '-') {
                return false;
            }
            ++label;
        } else {
            return false;
        }
    }
    return true;
}

SourcesRecovery recoverSourcesFile(const char* path) noexcept
{
    char temporary[512];
    char backup[512];
    if (!withSuffix(temporary, path, ".tmp") || !withSuffix(backup, path, ".bak")) {
        return SourcesRecovery::Nothing;
    }
    const bool hasTmp = exists(temporary);
    const bool hasBackup = exists(backup);
    if (!hasTmp && !hasBackup) {
        return SourcesRecovery::Nothing;
    }
    // A .tmp is never trusted: nothing tells a complete one from a cut one.
    std::remove(temporary);
    if (exists(path)) {
        std::remove(backup);  // `path` is whole (the old text, or the new one before the .bak went)
        return SourcesRecovery::Discarded;
    }
    if (hasBackup) {
        // Cut between moving the old file aside and naming the new one: the old one comes back.
        return sourcesFileRename(backup, path) == 0 ? SourcesRecovery::Restored : SourcesRecovery::Failed;
    }
    return SourcesRecovery::Discarded;  // a first save cut short: there was no file before
}

bool writeSourcesFile(const char* path, std::size_t index, const char* name, char* error,
                      std::size_t errorSize) noexcept
{
    char temporary[512];
    char backup[512];
    if (!withSuffix(temporary, path, ".tmp") || !withSuffix(backup, path, ".bak")) {
        std::snprintf(error, errorSize, "path too long");
        return false;
    }
    // A .bak with no file is the last whole text, left by a save whose renames both failed:
    // it comes back before anything is read, or nothing is saved (an empty read would drop
    // the other sources).
    if (!exists(path) && exists(backup) && sourcesFileRename(backup, path) != 0) {
        std::snprintf(error, errorSize, "cannot restore %s", backup);
        return false;
    }
    // "r+b" also proves the file can be written: a read-only file stops here. A file that
    // does not exist reads as an empty one, so the first source saved creates it (T5, C6).
    errno = 0;
    std::FILE* in = std::fopen(path, "r+b");
    if (in == nullptr && errno != ENOENT) {
        std::snprintf(error, errorSize, "cannot open %s for writing", path);
        return false;
    }
    std::FILE* out = std::fopen(temporary, "wb");
    if (out == nullptr) {
        if (in != nullptr) {
            std::fclose(in);
        }
        std::snprintf(error, errorSize, "cannot create %s", temporary);
        return false;
    }
    bool ok = true;
    bool written = false;
    bool lineStart = true;  // the next chunk fgets gives starts a line
    bool endsWithNewline = true;
    bool skipping = false;  // the rest of the replaced line is dropped
    std::size_t sources = 0;
    char line[256];
    while (ok && in != nullptr && std::fgets(line, sizeof line, in) != nullptr) {
        const std::size_t length = std::strlen(line);
        const bool complete = length > 0 && line[length - 1] == '\n';
        if (lineStart) {
            skipping = false;
            if (isSourceLine(line) && sources++ == index) {
                ok = name == nullptr || std::fprintf(out, "%s\n", name) > 0;  // null: deleted
                written = true;
                skipping = true;
            }
        }
        if (!skipping) {
            ok = ok && std::fwrite(line, 1, length, out) == length;
        }
        lineStart = complete;
        endsWithNewline = complete;
    }
    ok = ok && (in == nullptr || std::ferror(in) == 0);
    if (ok && !written) {
        if (sources != index || name == nullptr) {
            ok = false;
            std::snprintf(error, errorSize, "source %u is not in the file (it has %u)",
                          static_cast<unsigned>(index + 2), static_cast<unsigned>(sources));
        } else {
            ok = (endsWithNewline || std::fputc('\n', out) != EOF) && std::fprintf(out, "%s\n", name) > 0;
        }
    } else if (!ok) {
        std::snprintf(error, errorSize, "read or write error");
    }
    if (in != nullptr) {
        std::fclose(in);
    }
    ok = std::fclose(out) == 0 && ok;
    if (ok && !replaceFile(temporary, path, backup)) {
        std::snprintf(error, errorSize, "cannot replace %s", path);
        ok = false;
    }
    if (!ok) {
        std::remove(temporary);
    }
    return ok;
}

}  // namespace rb::host
