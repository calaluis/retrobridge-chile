#pragma once

// Configured sources of the menu (Issue #40). Source 1 is always the local one
// (--images, shown as "SD"); sources 2..8 come from --sources: one per line,
// in order, blank lines and lines starting with '#' skipped, surrounding spaces
// trimmed. Each name is 1..kMaxName printable ASCII characters. They are only
// shown: opening a server needs a TNFS client, which does not exist yet.

#include <array>
#include <cstddef>
#include <cstdio>

namespace rb::host {

class MenuSources {
public:
    static constexpr std::size_t kMaxSources = 7;  // sources 2..8
    static constexpr std::size_t kMaxName = 32;     // "n  " + name fits the reference's 35 columns

    std::size_t count() const noexcept { return count_; }
    // Source `number` (2..8), or nullptr if not configured.
    const char* name(unsigned number) const noexcept;

    // Reads the list from `file`. On failure the list is left empty and `error`
    // says why (line number included), for the user.
    bool read(std::FILE* file, char* error, std::size_t errorSize) noexcept;

private:
    std::array<std::array<char, kMaxName + 1>, kMaxSources> names_{};
    std::size_t count_ = 0;
};

}  // namespace rb::host
