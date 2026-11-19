#pragma once

// TnfsFolder of retrobridge_host (menu-tnfs T010): the result of listing a
// folder of a TNFS source, ready for MenuController to draw without allocating.
//
// Filter and order (plan §0): only folders and names ending in ".atr" (any
// case); no entry whose name starts with '.'; folders first, then images, each
// group in case-insensitive alphabetical order; at most kCapacity entries;
// total() counts every entry kept by the filter. An entry whose path would be
// longer than kMaxPath is not kept (it could never be opened).
//
// Display rule (as in #40): ASCII 'a'-'z' become upper case; every character
// that is not printable ASCII (0x20-0x7E) becomes one '?'. A multi-byte UTF-8
// character (its lead byte and its continuation bytes 0x80-0xBF) gives a single
// '?', and so does a stray byte. path() gives the original bytes, which is what
// the loader opens; it is built on demand from the folder and the entry's name.
//
// Storage (menu-navegacion N002, M42-3): every TnfsFolder reserves one fixed block
// for kCapacity entries when it is created (new (std::nothrow), outside the SIO
// path); ready() says whether it got it. fill() runs on the loader thread and
// allocates only temporaries; swap() exchanges the blocks, so handing a listing
// over costs the same with 20 entries as with 999. The accessors do not allocate.

#include <cstddef>
#include <cstdint>
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
    static constexpr std::size_t kCapacity = 999;     // entries kept (M42-3)
    static constexpr std::size_t kPageRows = 20;      // rows of one screen of the list
    static constexpr std::size_t kDisplayWidth = 38;  // columns of a list row after the letter
    static constexpr std::size_t kMaxPath = 255;      // bytes of a TNFS path (rb::net_tnfs::kMaxPath)
    // Longest notice: "MOSTRANDO 20 DE " and the digits of a size_t.
    static constexpr std::size_t kNoticeSize = 40;

    struct Entry {
        bool isDir = false;
        char display[kDisplayWidth + 1] = {};  // folders end in '/'
        uint16_t nameLength = 0;
        char name[kMaxPath + 1] = {};  // original bytes, as READDIR gave them
    };

    TnfsFolder() noexcept;  // reserves the block; ready() is false if that failed
    ~TnfsFolder();
    TnfsFolder(TnfsFolder&& other) noexcept;
    TnfsFolder(const TnfsFolder&) = delete;
    TnfsFolder& operator=(const TnfsFolder&) = delete;
    TnfsFolder& operator=(TnfsFolder&&) = delete;

    // folder: the path that was listed ("/" for the root of the source).
    static TnfsFolder build(const std::string& folder, std::vector<TnfsListing> listed);
    // The same into this folder's block. false (and nothing kept) without a block.
    bool fill(const std::string& folder, std::vector<TnfsListing> listed);
    // Forgets the entries; keeps the block.
    void clear() noexcept;

    bool ready() const noexcept { return storage_ != nullptr; }
    std::size_t total() const noexcept { return storage_ != nullptr ? storage_->total : 0; }
    std::size_t count() const noexcept { return storage_ != nullptr ? storage_->count : 0; }
    // index < count().
    const Entry& entry(std::size_t index) const noexcept { return storage_->entries[index]; }
    // The path of entry index (< count()) into out, NUL-terminated; returns its length.
    std::size_t path(std::size_t index, char (&out)[kMaxPath + 1]) const noexcept;

    // The message under the key footer: "CARPETA VACIA" with no entry,
    // "MOSTRANDO 20 DE <total>" when more than kPageRows were kept, "" otherwise.
    void notice(char (&out)[kNoticeSize]) const noexcept;

    // Exchanges the blocks: no allocation, no entry visited (T011: the loader
    // hands a listing to the menu thread this way).
    void swap(TnfsFolder& other) noexcept;

private:
    struct Storage {
        Entry entries[kCapacity];
        char folder[kMaxPath + 1] = {};
        std::size_t folderLength = 0;
        std::size_t count = 0;
        std::size_t total = 0;
    };
    Storage* storage_ = nullptr;
};

// The display form of `length` bytes of text: at most `width` characters into
// out (which holds width + 1), NUL-terminated. Returns the characters written.
std::size_t tnfsDisplay(const char* text, std::size_t length, char* out, std::size_t width) noexcept;

// A path for the list title, in display form: when it needs more than `width`
// characters, it is cut on the left and starts with "..", so the current
// folder stays visible. out holds width + 1.
void tnfsTitle(const std::string& path, std::size_t width, char* out) noexcept;
// The same over `length` bytes, without a std::string (MenuController draws the
// title in key(), which must not allocate).
void tnfsTitle(const char* path, std::size_t length, std::size_t width, char* out) noexcept;

}  // namespace rb::host
