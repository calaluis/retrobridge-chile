#pragma once

// Configured sources of the menu (Issue #40). Source 1 is always the local one
// (--images, shown as "SD"); sources 2..8 come from --sources: one per line,
// in order, blank lines and lines starting with '#' skipped, surrounding spaces
// trimmed. Each name is 1..kMaxName printable ASCII characters. A source is
// opened with the TNFS client (menu-tnfs); E on the main screen edits or adds
// one and rewrites the file (menu-editar-servidor).

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

    // Source `number` (2..8) becomes `name` (already valid). A number past the
    // last configured source adds one: it takes the first free number, as the
    // file has no gaps (M43). Returns the number it got, 0 if the list is full.
    unsigned set(unsigned number, const char* name) noexcept;

    // Source `number` (2..8) leaves the list; the ones after it move up one
    // number (M44). False if it is not configured.
    bool remove(unsigned number) noexcept;

    // A server name or an IPv4 address of 1..kMaxName characters (M43-1). IPv4:
    // four decimal numbers 0..255 joined by '.', no leading zeros. Name: labels
    // of 1..63 letters, digits and '-' joined by '.', no label starts or ends
    // with '-'. Text with only digits and dots must be an IPv4 address.
    static bool validServer(const char* text) noexcept;

private:
    std::array<std::array<char, kMaxName + 1>, kMaxSources> names_{};
    std::size_t count_ = 0;
};

// Rewrites the sources file `path` (M43): its source line number `index`
// (0-based, counted as read() counts them) becomes `name`; if the file has
// exactly `index` source lines, `name` is added at the end. A null `name`
// deletes that source line (M44); there must be one. Comments, blank
// lines and the other lines are kept byte for byte. The new text goes to
// `path`.tmp, which then replaces `path`: on failure `path` is unchanged and
// `error` says why.
bool writeSourcesFile(const char* path, std::size_t index, const char* name, char* error,
                      std::size_t errorSize) noexcept;

}  // namespace rb::host
