#include "menu_catalog.hpp"

#include <cstdio>
#include <cstring>

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

bool isCatalogName(const char* fileName) noexcept
{
    const std::size_t length = std::strlen(fileName);
    if (length <= 4 || length > MenuCatalog::kMaxName) {
        return false;
    }
    const char* ext = fileName + length - 4;
    if (ext[0] != '.' || lower(ext[1]) != 'a' || lower(ext[2]) != 't' || lower(ext[3]) != 'r') {
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

bool readCatalog(const char* directory, MenuCatalog& catalog)
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
            catalog.offer(entry.cFileName);
        }
    } while (FindNextFileA(find, &entry));
    FindClose(find);
    return true;
#else
    DIR* dir = opendir(directory);
    if (dir == nullptr) {
        return false;
    }
    char path[4096];
    while (const dirent* entry = readdir(dir)) {
        const int written = std::snprintf(path, sizeof path, "%s/%s", directory, entry->d_name);
        struct stat info {};
        if (written > 0 && static_cast<std::size_t>(written) < sizeof path && stat(path, &info) == 0 &&
            S_ISREG(info.st_mode)) {
            catalog.offer(entry->d_name);
        }
    }
    closedir(dir);
    return true;
#endif
}

}  // namespace rb::host
