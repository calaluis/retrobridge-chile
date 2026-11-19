#pragma once

// TnfsFolder of retrobridge_host (menu-tnfs T010): the result of listing a
// folder of a TNFS source, ready for MenuController to draw without allocating.
//
// Filter and order (plan §0): only folders and names ending in ".atr" (any
// case); no entry whose name starts with '.'; folders first, then images, each
// group in case-insensitive alphabetical order; at most kMaxShown entries;
// total() counts every entry kept by the filter.
//
// Display rule (as in #40): ASCII 'a'-'z' become upper case; every character
// that is not printable ASCII (0x20-0x7E) becomes one '?'. A multi-byte UTF-8
// character (its lead byte and its continuation bytes 0x80-0xBF) gives a single
// '?', and so does a stray byte. path keeps the original bytes and is what the
// loader opens.
//
// build() runs on the loader thread and allocates; the accessors do not.

#include <cstddef>
#include <string>
#include <vector>

namespace rb::host {

// One entry of a TNFS directory as the loader saw it (READDIR name, STAT kind).
struct TnfsListing {
    std::string name;
    bool isDir = false;
};

class TnfsFolder {
public:
    static constexpr std::size_t kMaxShown = 20;
    static constexpr std::size_t kDisplayWidth = 38;  // columns of a list row after the letter
    // Longest notice: "MOSTRANDO 20 DE " and the digits of a size_t.
    static constexpr std::size_t kNoticeSize = 40;

    struct Entry {
        bool isDir = false;
        char display[kDisplayWidth + 1] = {};  // folders end in '/'
        std::string path;                      // original bytes: folder + '/' + name
    };

    // folder: the path that was listed ("/" for the root of the source).
    static TnfsFolder build(const std::string& folder, std::vector<TnfsListing> listed);

    std::size_t total() const noexcept { return total_; }
    std::size_t shown() const noexcept { return shown_; }
    const Entry& entry(std::size_t index) const noexcept { return entries_[index]; }

    // The message under the key footer: "CARPETA VACIA" with no entry,
    // "MOSTRANDO 20 DE <total>" when more than kMaxShown were kept, "" otherwise.
    void notice(char (&out)[kNoticeSize]) const noexcept;

private:
    Entry entries_[kMaxShown];
    std::size_t shown_ = 0;
    std::size_t total_ = 0;
};

// The display form of `length` bytes of text: at most `width` characters into
// out (which holds width + 1), NUL-terminated. Returns the characters written.
std::size_t tnfsDisplay(const char* text, std::size_t length, char* out, std::size_t width) noexcept;

// A path for the list title, in display form: when it needs more than `width`
// characters, it is cut on the left and starts with "..", so the current
// folder stays visible. out holds width + 1.
void tnfsTitle(const std::string& path, std::size_t width, char* out) noexcept;

}  // namespace rb::host
