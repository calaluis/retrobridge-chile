#include "rb/menu/wifi_config.hpp"

#include <cstring>

namespace rb::host {

namespace {

enum class Line { Ok, TooLong, End, Error };

void put(char* out, std::size_t max, std::size_t& length, bool& tooLong, char c) noexcept
{
    if (length < max) {
        out[length] = c;
    } else {
        tooLong = true;
    }
    ++length;
}

// Reads one line into `out` (at most `max` bytes, NUL added) without its line end: LF, CR LF,
// or a CR at the end of the file. A CR is held back until the next byte shows it is not part of
// the line end, so it never counts against `max` (T5 review: files saved on Windows). End: the
// file ended before any byte of this line.
Line readLine(std::FILE* file, char* out, std::size_t max, std::size_t& length) noexcept
{
    length = 0;
    bool any = false;
    bool tooLong = false;
    bool cr = false;  // a CR read and not stored yet
    for (;;) {
        const int c = std::fgetc(file);
        if (c == EOF) {
            if (std::ferror(file) != 0) {
                return Line::Error;
            }
            break;  // a held CR at the end of the file is a line end
        }
        any = true;
        if (c == '\n') {
            break;  // a held CR before it is part of the line end
        }
        if (cr) {
            put(out, max, length, tooLong, '\r');  // a CR inside the line is text
            cr = false;
        }
        if (c == '\r') {
            cr = true;
        } else {
            put(out, max, length, tooLong, static_cast<char>(c));
        }
    }
    if (length <= max) {
        out[length] = '\0';
    }
    if (!any) {
        return Line::End;
    }
    return tooLong ? Line::TooLong : Line::Ok;
}

}  // namespace

WifiConfig::Result WifiConfig::read(std::FILE* file) noexcept
{
    wipe();
    Result result = Result::Ok;
    const Line name = readLine(file, name_.data(), kMaxName, nameLength_);
    if (name == Line::Error) {
        result = Result::ReadError;
    } else if (name != Line::Ok || nameLength_ == 0) {
        result = Result::BadName;
    } else {
        const Line key = readLine(file, key_.data(), kMaxKey, keyLength_);
        if (key == Line::Error) {
            result = Result::ReadError;
        } else if (key == Line::TooLong || (keyLength_ != 0 && keyLength_ < kMinKey)) {
            result = Result::BadKey;
        } else {
            // Nothing but line ends after line 2.
            for (int c = std::fgetc(file); c != EOF; c = std::fgetc(file)) {
                if (c != '\r' && c != '\n') {
                    result = Result::ExtraLines;
                    break;
                }
            }
            if (result == Result::Ok && std::ferror(file) != 0) {
                result = Result::ReadError;
            }
        }
    }
    if (result != Result::Ok) {
        wipe();
    }
    return result;
}

void WifiConfig::wipe() noexcept
{
    volatile char* name = name_.data();
    for (std::size_t i = 0; i < name_.size(); ++i) {
        name[i] = 0;
    }
    volatile char* key = key_.data();
    for (std::size_t i = 0; i < key_.size(); ++i) {
        key[i] = 0;
    }
    nameLength_ = 0;
    keyLength_ = 0;
}

const char* WifiConfig::reason(Result result) noexcept
{
    switch (result) {
    case Result::Ok:
        return "ok";
    case Result::BadName:
        return "network name missing or longer than 32 bytes";
    case Result::BadKey:
        return "key not empty and not 8..63 bytes";
    case Result::ExtraLines:
        return "text after line 2";
    case Result::ReadError:
        break;
    }
    return "read error";
}

}  // namespace rb::host
