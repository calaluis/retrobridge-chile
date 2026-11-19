#include "tnfs_folder.hpp"

#include <algorithm>
#include <cstdio>
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
    return entry.isDir || hasAtrExtension(entry.name.c_str());
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
    const std::size_t total = displayLength(path.data(), path.size());
    if (total <= width) {
        tnfsDisplay(path.data(), path.size(), out, width);
        return;
    }
    if (width < 2) {
        tnfsDisplay("..", width, out, width);
        return;
    }
    // Keep the last width - 2 characters after "..".
    DisplayCursor cursor(path.data(), path.size());
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

TnfsFolder TnfsFolder::build(const std::string& folder, std::vector<TnfsListing> listed)
{
    listed.erase(std::remove_if(listed.begin(), listed.end(), [](const TnfsListing& e) { return !kept(e); }),
                 listed.end());
    std::sort(listed.begin(), listed.end(), [](const TnfsListing& a, const TnfsListing& b) {
        if (a.isDir != b.isDir) {
            return a.isDir;
        }
        return compareNames(a.name.c_str(), b.name.c_str()) < 0;
    });

    TnfsFolder result;
    result.total_ = listed.size();
    result.shown_ = std::min(listed.size(), kMaxShown);
    const bool root = !folder.empty() && folder.back() == '/';
    for (std::size_t i = 0; i < result.shown_; ++i) {
        TnfsListing& source = listed[i];
        Entry& entry = result.entries_[i];
        entry.isDir = source.isDir;
        if (source.isDir) {
            const std::size_t written =
                tnfsDisplay(source.name.data(), source.name.size(), entry.display, kDisplayWidth - 1);
            entry.display[written] = '/';
            entry.display[written + 1] = '\0';
        } else {
            tnfsDisplay(source.name.data(), source.name.size(), entry.display, kDisplayWidth);
        }
        entry.path = root ? folder + source.name : folder + "/" + source.name;
    }
    return result;
}

void TnfsFolder::notice(char (&out)[kNoticeSize]) const noexcept
{
    if (total_ == 0) {
        std::snprintf(out, sizeof out, "CARPETA VACIA");
    } else if (total_ > kMaxShown) {
        std::snprintf(out, sizeof out, "MOSTRANDO %llu DE %llu", static_cast<unsigned long long>(kMaxShown),
                      static_cast<unsigned long long>(total_));
    } else {
        out[0] = '\0';
    }
}

}  // namespace rb::host
