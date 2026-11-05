#include "PortScheduler.hpp"

namespace {

void collectPortMask(void* context, const ch9344::RxRecordView& record)
{
    auto* mask = static_cast<std::uint8_t*>(context);
    if (record.logicalPort < ch9344::driver::kPortCount) {
        *mask |= static_cast<std::uint8_t>(1U << record.logicalPort);
    }
}

} // namespace

ch9344::driver::TxPortSelection ch9344::driver::selectNextTxPort(
    std::uint8_t readyMask,
    std::uint8_t cursor)
{
    const std::uint8_t normalizedCursor = cursor % kPortCount;
    const std::uint8_t validMask = readyMask & kAllPortsMask;
    for (std::uint8_t offset = 0; offset < kPortCount; ++offset) {
        const std::uint8_t port = (normalizedCursor + offset) % kPortCount;
        if ((validMask & static_cast<std::uint8_t>(1U << port)) != 0) {
            return {
                true,
                port,
                static_cast<std::uint8_t>((port + 1) % kPortCount),
            };
        }
    }
    return {false, 0, normalizedCursor};
}

void ch9344::driver::TxFlowControl::reset()
{
    readyMask = kAllPortsMask;
}

ch9344::driver::TxPortSelection ch9344::driver::TxFlowControl::select(
    std::uint8_t pendingMask, std::uint8_t cursor) const
{
    return selectNextTxPort(pendingMask & readyMask, cursor);
}

void ch9344::driver::TxFlowControl::submitted(std::uint8_t logicalPort)
{
    if (logicalPort < kPortCount) {
        readyMask &= static_cast<std::uint8_t>(~(1U << logicalPort));
    }
}

void ch9344::driver::TxFlowControl::ready(std::uint8_t portMask)
{
    readyMask |= portMask & kAllPortsMask;
}

ch9344::driver::CommandStatusInspection ch9344::driver::inspectCommandStatus(
    const std::uint8_t* transfer, std::size_t transferLength)
{
    if (transfer == nullptr && transferLength != 0) {
        return {false, 0};
    }
    std::uint8_t readyMask = 0;
    std::size_t offset = 0;
    while (offset < transferLength) {
        if (transferLength - offset < 2) {
            return {false, 0};
        }
        const std::uint8_t port = transfer[offset] & 0x0f;
        const std::uint8_t type = transfer[offset + 1];
        std::size_t length = 0;
        bool txEmpty = false;
        // Replies have different lengths. Never scan their payload for IIR bytes.
        if (type == 0xa1) {
            length = 12;
        } else if ((type >= 0x97 && type <= 0x9d) || type == 0x85 || type == 0xa9) {
            length = 4;
        } else if (type >= 0xa3 && type <= 0xa7) {
            length = type == 0xa7 ? 10 : 3;
        } else if ((type & 0x0f) == 0x02 || (type & 0x0f) == 0x00 ||
                   (type & 0x0f) == 0x06) {
            if (port < 4 || port > 7) {
                return {false, 0};
            }
            length = 3;
            txEmpty = (type & 0x0f) == 0x02;
        } else {
            return {false, 0};
        }
        if (length > transferLength - offset) {
            return {false, 0};
        }
        if (txEmpty) {
            readyMask |= static_cast<std::uint8_t>(1U << (port - 4));
        }
        offset += length;
    }
    return {true, readyMask};
}

ch9344::driver::RxPortInspection ch9344::driver::inspectRxPorts(
    const std::uint8_t* transfer,
    std::size_t transferLength)
{
    std::uint8_t portMask = 0;
    const ch9344::DecodeResult decoded = ch9344::decodeRxTransfer(
        transfer,
        transferLength,
        collectPortMask,
        &portMask);
    if (decoded.error != ch9344::Error::none) {
        return {decoded.error, 0};
    }
    return {ch9344::Error::none, portMask};
}

std::uint8_t ch9344::driver::markRxPortDelivered(
    std::uint8_t pendingMask,
    std::uint8_t logicalPort)
{
    if (logicalPort >= kPortCount) {
        return pendingMask;
    }
    return pendingMask & static_cast<std::uint8_t>(~(1U << logicalPort));
}
