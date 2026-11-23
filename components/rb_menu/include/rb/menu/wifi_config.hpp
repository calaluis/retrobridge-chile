#pragma once

// retrobridge-wifi.txt (menu-sio-wifi-tnfs plan §3, WifiConfig): line 1 the network name
// (1..32 bytes), line 2 the key (empty for an open network, or 8..63 bytes). Only the
// trailing CR/LF of each line is removed, so names and keys may hold spaces. Nothing but
// line ends may follow line 2. No method formats the key, and no result names it.

#include <array>
#include <cstddef>
#include <cstdio>

namespace rb::host {

class WifiConfig {
public:
    static constexpr std::size_t kMaxName = 32;
    static constexpr std::size_t kMinKey = 8;
    static constexpr std::size_t kMaxKey = 63;

    enum class Result { Ok, BadName, BadKey, ExtraLines, ReadError };

    WifiConfig() = default;
    WifiConfig(const WifiConfig&) = delete;
    WifiConfig& operator=(const WifiConfig&) = delete;
    ~WifiConfig() { wipe(); }

    // Reads `file` from where it is. On any result but Ok, nothing is kept.
    Result read(std::FILE* file) noexcept;

    const char* name() const noexcept { return name_.data(); }
    std::size_t nameLength() const noexcept { return nameLength_; }
    bool hasKey() const noexcept { return keyLength_ != 0; }
    // Only the Wi-Fi station takes it, to hand it to the driver; then wipe().
    const char* key() const noexcept { return key_.data(); }
    std::size_t keyLength() const noexcept { return keyLength_; }

    // Sets the name and the key to zero in RAM.
    void wipe() noexcept;

    // A short English reason for the log; it never holds the name or the key.
    static const char* reason(Result result) noexcept;

private:
    std::array<char, kMaxName + 1> name_{};
    std::array<char, kMaxKey + 1> key_{};
    std::size_t nameLength_ = 0;
    std::size_t keyLength_ = 0;
};

}  // namespace rb::host
