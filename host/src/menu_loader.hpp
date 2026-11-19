#pragma once

// What MenuController needs from the TNFS loading thread (menu-tnfs T012). The
// menu sees this interface only, so it does not depend on the TNFS client;
// TnfsLoader (T011) implements it.
//
// Every call runs on the menu (SIO) thread, never allocates or frees memory and
// never waits for the network (ADR-011). await() waits at most `micros` for the
// loader thread, so a 'K' is answered within 250 ms (plan §0).

#include <cstddef>
#include <cstdint>

#include "sha256.hpp"
#include "tnfs_folder.hpp"

namespace rb::host {

// Longest path a TNFS request carries (rb::net_tnfs::kMaxPath; tnfs_loader.cpp checks).
constexpr std::size_t kTnfsMaxPath = 255;

enum class LoaderError : uint8_t {
    NoResponde,     // no answer, a malformed answer, or the link failed
    NoEncontrado,   // the server name does not resolve, or the source has no server
    NoSePudoAbrir,  // the server refused, no memory, no free slot, path too long
};

struct LoaderStatus {
    enum class Kind : uint8_t { InProgress, Folder, Image, Error, Unknown };
    static constexpr std::size_t kCauseSize = 96;

    Kind kind = Kind::Unknown;
    uint8_t percent = 0;                    // InProgress
    std::size_t slot = 0;                   // Image: the caller now owns this slot
    uint32_t bytes = 0;                     // Image
    Sha256Digest sha256{};                  // Image
    LoaderError error = LoaderError::NoResponde;  // Error
    char cause[kCauseSize] = {};            // Error: for the host log, never for the screen
};

class MenuLoader {
public:
    MenuLoader(const MenuLoader&) = delete;
    MenuLoader& operator=(const MenuLoader&) = delete;

    // List the folder `path` of a source, or bring the image at `path` whole.
    // `path` is absolute UTF-8. Returns the new job's number (never 0); a job
    // in progress is cancelled.
    virtual uint32_t startList(unsigned source, const char* path, std::size_t length) noexcept = 0;
    virtual uint32_t startFetch(unsigned source, const char* path, std::size_t length) noexcept = 0;

    // The state of job `job`. A finished result is handed over once: a Folder is
    // swapped into `folder`; an Image gives its slot to the caller.
    virtual LoaderStatus poll(uint32_t job, TnfsFolder& folder) noexcept = 0;
    virtual void cancel(uint32_t job) noexcept = 0;

    // Returns when job `job` has a result, is no longer current, or `micros` passed.
    virtual void await(uint32_t job, uint32_t micros) noexcept = 0;

    // Gives back a slot the menu no longer uses (M41-20). The loader thread frees
    // it; while it is still open or attached, it stays marked for a later try.
    virtual void recycle(std::size_t slot) noexcept = 0;

protected:
    MenuLoader() = default;
    ~MenuLoader() = default;
};

}  // namespace rb::host
