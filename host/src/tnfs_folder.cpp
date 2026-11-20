#include "tnfs_folder.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <utility>

#include "menu_catalog.hpp"

namespace rb::host {
namespace {

// Walks text one display character at a time (see the display rule in the header).
class DisplayCursor {
public:
    DisplayCursor(const char* text, std::size_t length) noexcept : text_(text), length_(length) {}

    // false at the end; otherwise the next display character.
    bool next(char& out) noexcept
    {
        if (at_ >= length_) {
            return false;
        }
        const unsigned char c = static_cast<unsigned char>(text_[at_++]);
        if (c < 0x80) {
            if (c < 0x20 || c > 0x7E) {
                out = '?';
            } else {
                out = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : static_cast<char>(c);
            }
            return true;
        }
        // A lead byte, a continuation byte or an invalid byte: one '?' for it and
        // for every continuation byte that follows.
        while (at_ < length_ && (static_cast<unsigned char>(text_[at_]) & 0xC0) == 0x80) {
            ++at_;
        }
        out = '?';
        return true;
    }

private:
    const char* text_;
    std::size_t length_;
    std::size_t at_ = 0;
};

std::size_t displayLength(const char* text, std::size_t length) noexcept
{
    DisplayCursor cursor(text, length);
    std::size_t count = 0;
    char c = 0;
    while (cursor.next(c)) {
        ++count;
    }
    return count;
}

bool kept(const TnfsListing& entry) noexcept
{
    if (entry.name.empty() || entry.name[0] == '.') {
        return false;
    }
    return entry.isDir || hasImageExtension(entry.name.c_str());
}

}  // namespace

std::size_t tnfsDisplay(const char* text, std::size_t length, char* out, std::size_t width) noexcept
{
    DisplayCursor cursor(text, length);
    std::size_t written = 0;
    char c = 0;
    while (written < width && cursor.next(c)) {
        out[written++] = c;
    }
    out[written] = '\0';
    return written;
}

void tnfsTitle(const std::string& path, std::size_t width, char* out) noexcept
{
    tnfsTitle(path.data(), path.size(), width, out);
}

void tnfsTitle(const char* path, std::size_t length, std::size_t width, char* out) noexcept
{
    const std::size_t total = displayLength(path, length);
    if (total <= width) {
        tnfsDisplay(path, length, out, width);
        return;
    }
    if (width < 2) {
        tnfsDisplay("..", width, out, width);
        return;
    }
    // Keep the last width - 2 characters after "..".
    DisplayCursor cursor(path, length);
    char c = 0;
    for (std::size_t skip = total - (width - 2); skip > 0; --skip) {
        cursor.next(c);
    }
    out[0] = '.';
    out[1] = '.';
    std::size_t written = 2;
    while (written < width && cursor.next(c)) {
        out[written++] = c;
    }
    out[written] = '\0';
}

TnfsFolder::TnfsFolder() noexcept : storage_(new (std::nothrow) Storage) {}

TnfsFolder::~TnfsFolder() { delete storage_; }

TnfsFolder::TnfsFolder(TnfsFolder&& other) noexcept : storage_(other.storage_) { other.storage_ = nullptr; }

TnfsFolder TnfsFolder::build(const std::string& folder, std::vector<TnfsListing> listed)
{
    TnfsFolder result;
    result.fill(folder, std::move(listed));
    return result;
}

bool TnfsFolder::fill(const std::string& folder, std::vector<TnfsListing> listed)
{
    if (storage_ == nullptr) {
        return false;
    }
    clear();
    if (folder.size() > kMaxPath) {
        return true;  // nothing under it could be opened
    }
    const bool root = !folder.empty() && folder.back() == '/';
    const std::size_t prefix = folder.size() + (root ? 0 : 1);
    listed.erase(std::remove_if(listed.begin(), listed.end(),
                                [prefix](const TnfsListing& e) { return !kept(e) || prefix + e.name.size() > kMaxPath; }),
                 listed.end());
    std::sort(listed.begin(), listed.end(), [](const TnfsListing& a, const TnfsListing& b) {
        if (a.isDir != b.isDir) {
            return a.isDir;
        }
        return compareNames(a.name.c_str(), b.name.c_str()) < 0;
    });

    Storage& s = *storage_;
    std::memcpy(s.folder, folder.data(), folder.size());
    s.folder[folder.size()] = '\0';
    s.folderLength = folder.size();
    s.total = listed.size();
    s.count = std::min(listed.size(), kCapacity);
    for (std::size_t i = 0; i < s.count; ++i) {
        const TnfsListing& source = listed[i];
        Entry& entry = s.entries[i];
        entry.isDir = source.isDir;
        if (source.isDir) {
            const std::size_t written =
                tnfsDisplay(source.name.data(), source.name.size(), entry.display, kDisplayWidth - 1);
            entry.display[written] = '/';
            entry.display[written + 1] = '\0';
        } else {
            tnfsDisplay(source.name.data(), source.name.size(), entry.display, kDisplayWidth);
        }
        std::memcpy(entry.name, source.name.data(), source.name.size());
        entry.name[source.name.size()] = '\0';
        entry.nameLength = static_cast<uint16_t>(source.name.size());
    }
    return true;
}

void TnfsFolder::clear() noexcept
{
    if (storage_ != nullptr) {
        storage_->folderLength = 0;
        storage_->folder[0] = '\0';
        storage_->count = 0;
        storage_->total = 0;
    }
}

std::size_t TnfsFolder::path(std::size_t index, char (&out)[kMaxPath + 1]) const noexcept
{
    const Storage& s = *storage_;
    const Entry& entry = s.entries[index];
    const bool root = s.folderLength > 0 && s.folder[s.folderLength - 1] == '/';
    std::size_t length = s.folderLength;
    std::memcpy(out, s.folder, length);
    if (!root) {
        out[length++] = '/';
    }
    std::memcpy(out + length, entry.name, entry.nameLength);  // fill() kept length <= kMaxPath
    length += entry.nameLength;
    out[length] = '\0';
    return length;
}

void TnfsFolder::notice(std::size_t page, char (&out)[kNoticeSize]) const noexcept
{
    if (total() == 0) {
        std::snprintf(out, sizeof out, "CARPETA VACIA");
    } else {
        pageLine(count(), total(), page, out);
    }
}

void pageLine(std::size_t kept, std::size_t total, std::size_t page, char (&out)[TnfsFolder::kNoticeSize]) noexcept
{
    if (total <= TnfsFolder::kPageRows) {
        out[0] = '\0';
        return;
    }
    const auto n = static_cast<unsigned long long>(page + 1);
    const auto m = static_cast<unsigned long long>((kept + TnfsFolder::kPageRows - 1) / TnfsFolder::kPageRows);
    // Formatted with room for any size_t, then cut to the notice: GCC's -Wformat-truncation
    // cannot see that the numbers are small, and the line never needs more than 40 columns.
    char line[96];
    if (kept == total) {
        std::snprintf(line, sizeof line, "PAGINA %llu DE %llu (%llu)", n, m, static_cast<unsigned long long>(total));
    } else {
        std::snprintf(line, sizeof line, "PAGINA %llu DE %llu (%llu DE %llu)", n, m,
                      static_cast<unsigned long long>(kept), static_cast<unsigned long long>(total));
    }
    std::size_t i = 0;
    for (; i + 1 < sizeof out && line[i] != '\0'; ++i) {
        out[i] = line[i];
    }
    out[i] = '\0';
}

}  // namespace rb::host

namespace rb::host {

void TnfsFolder::swap(TnfsFolder& other) noexcept { std::swap(storage_, other.storage_); }

}  // namespace rb::host
