#include "rb/netsio/netsio_codec.hpp"

#include "rb/core/assert.hpp"

namespace rb::netsio {

using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;
using rb::core::Span;

namespace {

constexpr int kVariable = -1;  // data block: 1..kMaxDataBlock bytes
constexpr int kUnknown = -2;

// Parameter length of each ID (FEP-003), excluding the ID itself.
int parameterLength(uint8_t messageId) noexcept
{
    switch (messageId) {
    case id::kDataBlock:
        return kVariable;
    case id::kDataByte:
    case id::kCommandOffSync:
    case id::kCreditStatus:
    case id::kCreditUpdate:
        return 1;
    case id::kDataByteSync:
        return 2;
    case id::kSpeedChange:
        return 4;
    case id::kSyncResponse:
        return 5;
    case id::kCommandOff:
    case id::kCommandOn:
    case id::kMotorOff:
    case id::kMotorOn:
    case id::kProceedOff:
    case id::kProceedOn:
    case id::kInterruptOff:
    case id::kInterruptOn:
    case id::kDeviceDisconnected:
    case id::kDeviceConnected:
    case id::kPingRequest:
    case id::kPingResponse:
    case id::kAliveRequest:
    case id::kAliveResponse:
    case id::kWarmReset:
    case id::kColdReset:
        return 0;
    default:
        return kUnknown;
    }
}

}  // namespace

bool fromAtari(uint8_t messageId) noexcept
{
    switch (messageId) {
    case id::kDataByte:
    case id::kDataBlock:
    case id::kDataByteSync:
    case id::kCommandOff:
    case id::kCommandOn:
    case id::kCommandOffSync:
    case id::kMotorOff:
    case id::kMotorOn:
    case id::kSpeedChange:
    case id::kWarmReset:
    case id::kColdReset:
        return true;
    default:
        return false;
    }
}

Result<Message> decode(Span<const uint8_t> datagram, PeerProfile profile) noexcept
{
    if (datagram.empty()) {
        return Failure{ErrorCode::InvalidArgument};
    }
    const uint8_t messageId = datagram[0];
    const int expected = parameterLength(messageId);
    if (expected == kUnknown) {
        return Failure{ErrorCode::InvalidArgument};
    }

    const bool trailer = profile == PeerProfile::NetsiohubTrailingByte && fromAtari(messageId);
    std::size_t parameters = datagram.size() - 1;
    if (trailer) {
        if (parameters == 0) {
            return Failure{ErrorCode::InvalidArgument};  // the trailing byte is missing
        }
        --parameters;
    }

    if (expected == kVariable) {
        if (parameters == 0 || parameters > kMaxDataBlock) {
            return Failure{ErrorCode::InvalidArgument};
        }
    } else if (parameters != static_cast<std::size_t>(expected)) {
        return Failure{ErrorCode::InvalidArgument};
    }

    return Message{messageId, datagram.subspan(1, parameters), trailer,
                   trailer ? datagram[datagram.size() - 1] : static_cast<uint8_t>(0)};
}

std::size_t encodeSyncResponse(Span<uint8_t> out, uint8_t syncNumber, uint8_t ackType, uint8_t ackByte,
                               uint16_t writeSize) noexcept
{
    RB_ASSERT(out.size() >= kSyncResponseSize);
    out[0] = id::kSyncResponse;
    out[1] = syncNumber;
    out[2] = ackType;
    out[3] = ackByte;
    out[4] = static_cast<uint8_t>(writeSize & 0xFF);  // LSB first (FEP-003)
    out[5] = static_cast<uint8_t>(writeSize >> 8);
    return kSyncResponseSize;
}

std::size_t encodeDataBlock(Span<uint8_t> out, Span<const uint8_t> data) noexcept
{
    RB_ASSERT(!data.empty() && data.size() <= kMaxDataBlock && out.size() >= 1 + data.size());
    out[0] = id::kDataBlock;
    for (std::size_t i = 0; i < data.size(); ++i) {
        out[1 + i] = data[i];
    }
    return 1 + data.size();
}

std::size_t encodeCreditStatus(Span<uint8_t> out, uint8_t remainingCredit) noexcept
{
    RB_ASSERT(out.size() >= 2);
    out[0] = id::kCreditStatus;
    out[1] = remainingCredit;
    return 2;
}

std::size_t encodeSpeedChange(Span<uint8_t> out, uint32_t baud) noexcept
{
    RB_ASSERT(out.size() >= 5);
    out[0] = id::kSpeedChange;
    out[1] = static_cast<uint8_t>(baud & 0xFF);
    out[2] = static_cast<uint8_t>((baud >> 8) & 0xFF);
    out[3] = static_cast<uint8_t>((baud >> 16) & 0xFF);
    out[4] = static_cast<uint8_t>((baud >> 24) & 0xFF);
    return 5;
}

std::size_t encodeSignal(Span<uint8_t> out, uint8_t messageId) noexcept
{
    RB_ASSERT(out.size() >= 1 && parameterLength(messageId) == 0);
    out[0] = messageId;
    return 1;
}

}  // namespace rb::netsio
