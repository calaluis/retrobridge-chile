#include "rb/storage/stdio_byte_source.hpp"

#include <sys/stat.h>

#include <cerrno>
#include <cstdint>

namespace rb::storage {

using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;
using rb::core::Span;

namespace {

ErrorCode open_error(int error) noexcept
{
    return (error == ENOENT || error == ENOTDIR) ? ErrorCode::NotFound : ErrorCode::IoError;
}

}  // namespace

Result<void> StdioByteSource::open(const char* path, Access access)
{
    close();

    struct stat info {};
    if (stat(path, &info) != 0) {
        return Failure{open_error(errno)};
    }
    if (!S_ISREG(info.st_mode)) {
        return Failure{ErrorCode::NotFound};  // a directory or device is not an image
    }

    std::FILE* file = access == Access::ReadWriteIfAllowed ? std::fopen(path, "r+b") : nullptr;
    const bool writable = file != nullptr;
    if (file == nullptr) {
        file = std::fopen(path, "rb");
    }
    if (file == nullptr) {
        return Failure{open_error(errno)};
    }
    if (std::setvbuf(file, nullptr, _IONBF, 0) != 0 || std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return Failure{ErrorCode::IoError};
    }
    const long length = std::ftell(file);
    if (length < 0 || static_cast<unsigned long>(length) > UINT32_MAX) {
        std::fclose(file);
        return Failure{ErrorCode::IoError};
    }
    file_ = file;
    size_ = static_cast<uint32_t>(length);
    writable_ = writable;
    return {};
}

void StdioByteSource::close() noexcept
{
    if (file_ != nullptr) {
        (void)std::fclose(file_);  // the slot is freed even if fclose reports an error
        file_ = nullptr;
        size_ = 0;
        writable_ = false;
    }
}

Result<void> StdioByteSource::readAt(uint32_t offset, Span<uint8_t> out)
{
    if (offset > size_ || out.size() > size_ - offset) {
        return Failure{ErrorCode::OutOfBounds};
    }
    if (out.empty()) {
        return {};
    }
    if (file_ == nullptr || std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0) {
        return Failure{ErrorCode::IoError};
    }
    std::size_t done = 0;
    while (done < out.size()) {
        const std::size_t got = std::fread(out.data() + done, 1, out.size() - done, file_);
        if (got == 0) {
            std::clearerr(file_);  // EOF (file shrank) or error: report, keep the stream usable
            return Failure{ErrorCode::IoError};
        }
        done += got;
    }
    return {};
}

Result<void> StdioByteSource::writeAt(uint32_t offset, Span<const uint8_t> in)
{
    if (offset > size_ || in.size() > size_ - offset) {
        return Failure{ErrorCode::OutOfBounds};
    }
    if (!writable_) {
        return Failure{ErrorCode::WriteProtected};
    }
    if (in.empty()) {
        return {};
    }
    if (file_ == nullptr || std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0) {
        return Failure{ErrorCode::IoError};
    }
    std::size_t done = 0;
    while (done < in.size()) {
        const std::size_t put = std::fwrite(in.data() + done, 1, in.size() - done, file_);
        if (put == 0) {
            std::clearerr(file_);
            return Failure{ErrorCode::IoError};
        }
        done += put;
    }
    if (std::fflush(file_) != 0) {
        std::clearerr(file_);
        return Failure{ErrorCode::IoError};
    }
    return {};
}

}  // namespace rb::storage
