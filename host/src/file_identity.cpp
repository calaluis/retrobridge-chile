#include "file_identity.hpp"

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace rb::host {

namespace {

#ifdef _WIN32
// Volume serial number plus the 128-bit file id: the identity NTFS and ReFS keep
// for a file across all its names. The handle asks for no data access, so the
// file is neither read nor changed.
bool identify(const char* path, FILE_ID_INFO& id) noexcept
{
    const HANDLE h = ::CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                   OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    const bool ok = ::GetFileInformationByHandleEx(h, FileIdInfo, &id, sizeof id) != 0;
    ::CloseHandle(h);
    return ok;
}
#else
bool identify(const char* path, struct stat& id) noexcept { return ::stat(path, &id) == 0; }
#endif

}  // namespace

FileIdentity compareFiles(const char* a, const char* b) noexcept
{
#ifdef _WIN32
    FILE_ID_INFO ia{};
    FILE_ID_INFO ib{};
    if (!identify(a, ia) || !identify(b, ib)) {
        return FileIdentity::Unknown;
    }
    const bool same = ia.VolumeSerialNumber == ib.VolumeSerialNumber &&
                      std::memcmp(&ia.FileId, &ib.FileId, sizeof ia.FileId) == 0;
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

}  // namespace rb::host
