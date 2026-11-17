// File identity for the host executable: tells whether two paths name the same
// file on disk, whatever their spelling (case, relative or absolute path, "..",
// 8.3 short name, hard link, symbolic link). Used to refuse a --log that would
// append to the --image being served.
#pragma once

namespace rb::host {

enum class FileIdentity {
    Different,  // both exist and are different files
    Same,       // both exist and are the same file
    Unknown,    // at least one could not be identified (missing, no access)
};

FileIdentity compareFiles(const char* a, const char* b) noexcept;

// --images (Issue #38): is `log` one of the .atr files (any case, any name) of
// `folder`, in the menu or not? On Windows the folder is listed and opened with
// the wide API, so names outside the ANSI code page are checked too. `match`
// receives the matching name (UTF-8 on Windows), truncated to its size.
enum class FolderCheck {
    Clear,       // log does not exist yet, or is no .atr of the folder
    Matches,     // log is an .atr of the folder
    Unreadable,  // the folder could not be listed completely: treat as a match
};

FolderCheck findAtrIdenticalTo(const char* log, const char* folder, char* match, unsigned matchSize) noexcept;

}  // namespace rb::host
