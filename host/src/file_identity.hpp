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

}  // namespace rb::host
