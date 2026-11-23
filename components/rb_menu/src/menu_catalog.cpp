#include "rb/menu/menu_catalog.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace rb::host {

namespace {

char lower(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

}  // namespace

int compareNames(const char* a, const char* b) noexcept
{
    for (std::size_t i = 0;; ++i) {
        const char x = lower(a[i]);
        const char y = lower(b[i]);
        if (x != y) {
            return x < y ? -1 : 1;
        }
        if (x == '\0') {
            break;
        }
    }
    const int exact = std::strcmp(a, b);
    return exact < 0 ? -1 : (exact > 0 ? 1 : 0);
}

namespace {

// True if the name is longer than ".ext" and ends in '.' plus `ext` (three lowercase letters), any case.
bool hasExtension(const char* fileName, const char* ext) noexcept
{
    const std::size_t length = std::strlen(fileName);
    if (length <= 4) {
        return false;
    }
    const char* tail = fileName + length - 4;
    return tail[0] == '.' && lower(tail[1]) == ext[0] && lower(tail[2]) == ext[1] && lower(tail[3]) == ext[2];
}

}  // namespace

bool hasAtrExtension(const char* fileName) noexcept { return hasExtension(fileName, "atr"); }

bool hasXexExtension(const char* fileName) noexcept { return hasExtension(fileName, "xex"); }

bool hasImageExtension(const char* fileName) noexcept
{
    return hasAtrExtension(fileName) || hasXexExtension(fileName);
}

bool hasCasExtension(const char* fileName) noexcept { return hasExtension(fileName, "cas"); }

EntryKind entryKind(const char* fileName) noexcept
{
    if (hasCasExtension(fileName)) {
        return EntryKind::Tape;
    }
    return hasXexExtension(fileName) ? EntryKind::Xex : EntryKind::Disk;
}

bool isCatalogName(const char* fileName) noexcept
{
    if (!(hasImageExtension(fileName) || hasCasExtension(fileName)) || std::strlen(fileName) > MenuCatalog::kMaxName) {
        return false;
    }
    // build_image_path also refuses ':' and '\'; such a file could be listed but never opened.
    if (std::strchr(fileName, ':') != nullptr || std::strchr(fileName, '\\') != nullptr) {
        return false;
    }
    return rb::app::ImageLocation::from(fileName).ok();
}

bool MenuCatalog::offer(const char* fileName)
{
    if (!isCatalogName(fileName)) {
        return false;
    }
    ++atrSeen_;
    std::size_t at = 0;
    while (at < count_ && compareNames(names_[at].data(), fileName) < 0) {
        ++at;
    }
    if (at == kMaxEntries) {
        return false;
    }
    if (at < count_ && compareNames(names_[at].data(), fileName) == 0) {
        return false;  // the same name twice
    }
    const std::size_t last = count_ < kMaxEntries ? count_ : kMaxEntries - 1;
    for (std::size_t i = last; i > at; --i) {
        names_[i] = names_[i - 1];
    }
    std::memcpy(names_[at].data(), fileName, std::strlen(fileName) + 1);
    if (count_ < kMaxEntries) {
        ++count_;
    }
    return true;
}

std::size_t MenuCatalog::insert(const char* fileName)
{
    if (full() || !isCatalogName(fileName)) {
        return kNotInserted;
    }
    std::size_t at = 0;
    while (at < count_ && compareNames(names_[at].data(), fileName) < 0) {
        ++at;
    }
    if (at < count_ && compareNames(names_[at].data(), fileName) == 0) {
        return kNotInserted;
    }
    return offer(fileName) ? at : kNotInserted;
}

bool readCatalog(const char* directory, MenuCatalog& catalog)
{
    return forEachRegularFile(
        directory, [](const char* name, void* context) { static_cast<MenuCatalog*>(context)->offer(name); },
        &catalog);
}

bool forEachRegularFile(const char* directory, void (*visit)(const char* fileName, void* context), void* context)
{
#ifdef _WIN32
    char pattern[MAX_PATH];
    const int written = std::snprintf(pattern, sizeof pattern, "%s\\*", directory);
    if (written < 0 || static_cast<std::size_t>(written) >= sizeof pattern) {
        return false;
    }
    WIN32_FIND_DATAA entry;
    HANDLE find = FindFirstFileA(pattern, &entry);
    if (find == INVALID_HANDLE_VALUE) {
        return false;
    }
    do {
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            visit(entry.cFileName, context);
        }
    } while (FindNextFileA(find, &entry));
    FindClose(find);
    return true;
#else
    // The path buffer lives on the heap: on the ESP32 this runs on app_main, whose stack (3584 B by
    // default) is smaller than the buffer (menu-sio bench, 2026-11-21: Double exception in vfs_fat_stat).
    constexpr std::size_t kPathCapacity = 4096;  // longer paths are skipped, as before
    const std::unique_ptr<char[]> path{new (std::nothrow) char[kPathCapacity]};
    if (path == nullptr) {
        errno = ENOMEM;
        return false;
    }
    DIR* dir = opendir(directory);
    if (dir == nullptr) {
        return false;
    }
    while (const dirent* entry = readdir(dir)) {
        const int written = std::snprintf(path.get(), kPathCapacity, "%s/%s", directory, entry->d_name);
        struct stat info {};
        if (written > 0 && static_cast<std::size_t>(written) < kPathCapacity && stat(path.get(), &info) == 0 &&
            S_ISREG(info.st_mode)) {
            visit(entry->d_name, context);
        }
    }
    closedir(dir);
    return true;
#endif
}

}  // namespace rb::host
