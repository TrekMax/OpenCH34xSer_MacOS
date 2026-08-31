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
