#include "menu_sources.hpp"

#include <cstring>

namespace rb::host {

const char* MenuSources::name(unsigned number) const noexcept
{
    if (number < 2 || number - 2 >= count_) {
        return nullptr;
    }
    return names_[number - 2].data();
}

bool MenuSources::read(std::FILE* file, char* error, std::size_t errorSize) noexcept
{
    count_ = 0;
    char line[256];
    unsigned number = 0;
    while (std::fgets(line, sizeof line, file) != nullptr) {
        ++number;
        std::size_t length = std::strlen(line);
        if (length == sizeof line - 1 && line[length - 1] != '\n' && !std::feof(file)) {
            std::snprintf(error, errorSize, "line %u is longer than %u characters", number,
                          static_cast<unsigned>(kMaxName));
            count_ = 0;
            return false;
        }
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r' || line[length - 1] == ' ' ||
                              line[length - 1] == '\t')) {
            line[--length] = '\0';
        }
        const char* text = line;
        while (*text == ' ' || *text == '\t') {
            ++text;
            --length;
        }
        if (length == 0 || text[0] == '#') {
            continue;
        }
        if (length > kMaxName) {
            std::snprintf(error, errorSize, "line %u is longer than %u characters", number,
                          static_cast<unsigned>(kMaxName));
            count_ = 0;
            return false;
        }
        for (std::size_t i = 0; i < length; ++i) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if (c < 0x20 || c > 0x7E) {
                std::snprintf(error, errorSize, "line %u has a character outside printable ASCII", number);
                count_ = 0;
                return false;
            }
        }
        if (count_ == kMaxSources) {
            std::snprintf(error, errorSize, "more than %u sources (line %u)", static_cast<unsigned>(kMaxSources),
                          number);
            count_ = 0;
            return false;
        }
        std::memcpy(names_[count_].data(), text, length);
        names_[count_][length] = '\0';
        ++count_;
    }
    if (std::ferror(file) != 0) {
        std::snprintf(error, errorSize, "read error");
        count_ = 0;
        return false;
    }
    return true;
}

}  // namespace rb::host
