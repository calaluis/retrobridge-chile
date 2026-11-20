#include "cassette_deck.hpp"

#include <cstring>

namespace rb::host {

CasResult CassetteDeck::mount(const char* name, rb::core::Span<const uint8_t> cas)
{
    CasTape tape;
    const CasResult result = CasImage::index(cas, tape);
    if (result != CasResult::Ok) {
        return result;  // C: stays as it was
    }
    player_.unload();
    bytes_.assign(cas.begin(), cas.end());
    tape_ = static_cast<CasTape&&>(tape);
    std::strncpy(name_.data(), name, kMaxName);
    name_[kMaxName] = '\0';
    player_.load(&tape_, rb::core::Span<const uint8_t>(bytes_.data(), bytes_.size()));
    return CasResult::Ok;
}

void CassetteDeck::eject()
{
    player_.unload();
    bytes_.clear();
    bytes_.shrink_to_fit();
    tape_.blocks.clear();
    name_[0] = '\0';
}

}  // namespace rb::host
