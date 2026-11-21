#pragma once

// Catalog of the boot menu (Issue #38): the .atr and .xex (menu-xex) files of the --images folder,
// in case-insensitive alphabetical order, at most kMaxEntries (999,
// menu-navegacion M42-3; the list shows them a page at a time). Fixed
// storage; names longer than an ImageLocation or with characters outside
// printable ASCII are skipped, so every entry can be opened by the repository.

#include <array>
#include <cstddef>

#include "rb/app/image_location.hpp"

namespace rb::host {

class MenuCatalog {
public:
    static constexpr std::size_t kMaxEntries = 999;
    static constexpr std::size_t kMaxName = rb::app::ImageLocation::kMaxLength;

    // Offers one directory entry. Returns true if it was kept (for now: a later,
    // alphabetically smaller name can push it out of the first kMaxEntries).
    bool offer(const char* fileName);

    // Adds a file created while the host runs (menu-formatear-disco, F003) and
    // returns its position. kNotInserted when the list is full, the name is
    // already there or is not a catalog name; the list is then unchanged.
    // Every position at or after the returned one moves up by one.
    static constexpr std::size_t kNotInserted = kMaxEntries;
    std::size_t insert(const char* fileName);
    bool full() const noexcept { return count_ == kMaxEntries; }

    std::size_t count() const noexcept { return count_; }
    const char* name(std::size_t index) const noexcept { return names_[index].data(); }

    // Number of image names (.atr or .xex) offered, including those beyond kMaxEntries.
    std::size_t atrSeen() const noexcept { return atrSeen_; }

private:
    std::array<std::array<char, kMaxName + 1>, kMaxEntries> names_{};
    std::size_t count_ = 0;
    std::size_t atrSeen_ = 0;
};

// True if the name ends in ".atr" (any case), whatever else it contains.
bool hasAtrExtension(const char* fileName) noexcept;

// True if the name ends in ".xex" (any case), whatever else it contains (menu-xex).
bool hasXexExtension(const char* fileName) noexcept;

// The names the menu lists: ".atr" or ".xex" (menu-xex X003, M48).
bool hasImageExtension(const char* fileName) noexcept;

// True if the name ends in ".atr" or ".xex" (any case) and is a valid ImageLocation.
bool isCatalogName(const char* fileName) noexcept;

// Calls visit for every regular file of the directory (any name, no limit).
// false if the directory cannot be read, or with errno = ENOMEM if the path
// buffer (4 KB, on the heap, not the caller's stack) cannot be reserved.
bool forEachRegularFile(const char* directory, void (*visit)(const char* fileName, void* context), void* context);

// Case-insensitive ASCII comparison; ties broken byte by byte, so the order is total.
int compareNames(const char* a, const char* b) noexcept;

// Lists the regular files of a directory into the catalog. false if the
// directory cannot be read (errno = ENOMEM: no memory for the path buffer).
bool readCatalog(const char* directory, MenuCatalog& catalog);

}  // namespace rb::host
