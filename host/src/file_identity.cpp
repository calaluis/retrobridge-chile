#include "file_identity.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace rb::host {

namespace {

#ifdef _WIN32
// Volume serial number plus file id: the identity Windows keeps for a file
// across all its names. NTFS and ReFS give a 128-bit id (FileIdInfo). FAT32 and
// exFAT (an SD card) do not support that class, so the classic 64-bit file
// index is used there; files on one volume always take the same path, so two
// identities are comparable whenever they can be equal.
struct WinId {
    uint64_t volume;
    unsigned char file[16];
};

bool identifyHandle(HANDLE h, WinId& id) noexcept
{
    id = WinId{};
    FILE_ID_INFO info{};
    if (::GetFileInformationByHandleEx(h, FileIdInfo, &info, sizeof info) != 0) {
        id.volume = info.VolumeSerialNumber;
        std::memcpy(id.file, &info.FileId, sizeof id.file);
        return true;
    }
    BY_HANDLE_FILE_INFORMATION classic{};
    if (::GetFileInformationByHandle(h, &classic) == 0) {
        return false;
    }
    id.volume = classic.dwVolumeSerialNumber;
    const uint64_t index = (static_cast<uint64_t>(classic.nFileIndexHigh) << 32) | classic.nFileIndexLow;
    std::memcpy(id.file, &index, sizeof index);
    return true;
}

bool sameId(const WinId& a, const WinId& b) noexcept
{
    return a.volume == b.volume && std::memcmp(a.file, b.file, sizeof a.file) == 0;
}

// The handle asks for no data access, so the file is neither read nor changed.
template <typename Char, typename Open>
bool identifyWith(const Char* path, WinId& id, Open open) noexcept
{
    const HANDLE h = open(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                          FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    const bool ok = identifyHandle(h, id);
    ::CloseHandle(h);
    return ok;
}

bool identify(const char* path, WinId& id) noexcept { return identifyWith(path, id, ::CreateFileA); }
bool identifyWide(const wchar_t* path, WinId& id) noexcept { return identifyWith(path, id, ::CreateFileW); }
#else
bool identify(const char* path, struct stat& id) noexcept { return ::stat(path, &id) == 0; }
#endif

}  // namespace

FileIdentity compareFiles(const char* a, const char* b) noexcept
{
#ifdef _WIN32
    WinId ia{};
    WinId ib{};
    if (!identify(a, ia) || !identify(b, ib)) {
        return FileIdentity::Unknown;
    }
    const bool same = sameId(ia, ib);
#else
    struct stat ia {};
    struct stat ib {};
    if (!identify(a, ia) || !identify(b, ib)) {
        return FileIdentity::Unknown;
    }
    const bool same = ia.st_dev == ib.st_dev && ia.st_ino == ib.st_ino;
#endif
    return same ? FileIdentity::Same : FileIdentity::Different;
}

namespace {

template <typename Char>
bool endsWithImage(const Char* name) noexcept
{
    std::size_t length = 0;
    while (name[length] != 0) {
        ++length;
    }
    if (length <= 4) {
        return false;
    }
    const Char* ext = name + length - 4;
    auto lower = [](Char c) { return (c >= 'A' && c <= 'Z') ? static_cast<Char>(c - 'A' + 'a') : c; };
    if (ext[0] != '.') {
        return false;
    }
    const bool atr = lower(ext[1]) == 'a' && lower(ext[2]) == 't' && lower(ext[3]) == 'r';
    const bool xex = lower(ext[1]) == 'x' && lower(ext[2]) == 'e' && lower(ext[3]) == 'x';  // menu-xex X003
    return atr || xex;
}

}  // namespace

FolderCheck findAtrIdenticalTo(const char* log, const char* folder, char* match, unsigned matchSize) noexcept
{
    if (matchSize > 0) {
        match[0] = '\0';
    }
#ifdef _WIN32
    WinId logId{};
    if (!identify(log, logId)) {
        return FolderCheck::Clear;  // fopen will create a new file: it cannot be an existing image
    }
    wchar_t pattern[MAX_PATH + 8];
    const int length = ::MultiByteToWideChar(CP_ACP, 0, folder, -1, pattern, MAX_PATH);
    if (length <= 0) {
        return FolderCheck::Unreadable;
    }
    const std::size_t baseLength = static_cast<std::size_t>(length - 1);
    pattern[baseLength] = L'\\';
    pattern[baseLength + 1] = L'*';
    pattern[baseLength + 2] = L'\0';
    WIN32_FIND_DATAW entry;
    const HANDLE find = ::FindFirstFileW(pattern, &entry);
    if (find == INVALID_HANDLE_VALUE) {
        return FolderCheck::Unreadable;
    }
    FolderCheck result = FolderCheck::Clear;
    do {
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 || !endsWithImage(entry.cFileName)) {
            continue;
        }
        wchar_t path[MAX_PATH * 2 + 2];
        const std::size_t nameLength = std::wcslen(entry.cFileName);
        if (baseLength + 1 + nameLength >= sizeof path / sizeof path[0]) {
            result = FolderCheck::Unreadable;  // an image we cannot even name: refuse
            break;
        }
        std::wmemcpy(path, pattern, baseLength + 1);  // folder and '\'
        std::wmemcpy(path + baseLength + 1, entry.cFileName, nameLength + 1);
        WinId id{};
        if (!identifyWide(path, id)) {
            result = FolderCheck::Unreadable;  // an image we cannot identify: refuse
            break;
        }
        if (sameId(id, logId)) {
            if (matchSize > 0 &&
                ::WideCharToMultiByte(CP_UTF8, 0, entry.cFileName, -1, match, static_cast<int>(matchSize), nullptr,
                                      nullptr) == 0) {
                match[matchSize - 1] = '\0';
            }
            result = FolderCheck::Matches;
            break;
        }
    } while (::FindNextFileW(find, &entry));
    if (result == FolderCheck::Clear && ::GetLastError() != ERROR_NO_MORE_FILES) {
        result = FolderCheck::Unreadable;
    }
    ::FindClose(find);
    return result;
#else
    struct stat logId {};
    if (!identify(log, logId)) {
        return FolderCheck::Clear;  // fopen will create a new file: it cannot be an existing image
    }
    DIR* dir = ::opendir(folder);
    if (dir == nullptr) {
        return FolderCheck::Unreadable;
    }
    FolderCheck result = FolderCheck::Clear;
    char path[4096];
    while (const dirent* entry = ::readdir(dir)) {
        if (!endsWithImage(entry->d_name)) {
            continue;
        }
        const int written = std::snprintf(path, sizeof path, "%s/%s", folder, entry->d_name);
        struct stat id {};
        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof path) {
            result = FolderCheck::Unreadable;
            break;
        }
        if (::stat(path, &id) != 0) {
            continue;  // a dangling link: there is no file to write into
        }
        if (S_ISREG(id.st_mode) && id.st_dev == logId.st_dev && id.st_ino == logId.st_ino) {
            std::snprintf(match, matchSize, "%s", entry->d_name);
            result = FolderCheck::Matches;
            break;
        }
    }
    ::closedir(dir);
    return result;
#endif
}

}  // namespace rb::host
