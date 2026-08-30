#include <ch9344/Protocol.hpp>

#include <algorithm>
#include <cstring>

ch9344::Error ch9344::mapLogicalPort(uint8_t logicalPort, uint8_t* hardwarePort)
{
    if (hardwarePort == nullptr) {
        return Error::invalidArgument;
    }
    if (logicalPort >= 4) {
        return Error::invalidPort;
    }

    *hardwarePort = static_cast<uint8_t>(logicalPort + 4);
    return Error::none;
}

ch9344::TxFrameResult ch9344::encodeTxFrame(
    uint8_t logicalPort,
    const uint8_t* payload,
    std::size_t payloadLength,
    std::size_t maxPacketSize,
    uint8_t* output,
    std::size_t outputCapacity)
{
    uint8_t hardwarePort = 0;
    const Error portError = mapLogicalPort(logicalPort, &hardwarePort);
    if (portError != Error::none) {
        return {portError, 0, 0};
    }
    if (output == nullptr || (payload == nullptr && payloadLength != 0)) {
        return {Error::invalidArgument, 0, 0};
    }
    if (maxPacketSize <= 3) {
        return {Error::invalidPacketSize, 0, 0};
    }

    const std::size_t framePayload = std::min(payloadLength, maxPacketSize - 3);
    if (outputCapacity < framePayload + 3) {
        return {Error::outputTooSmall, 0, 0};
    }

    output[0] = hardwarePort;
    output[1] = static_cast<uint8_t>(framePayload & 0xff);
    output[2] = static_cast<uint8_t>((framePayload >> 8) & 0xff);
    if (framePayload != 0) {
        std::memcpy(output + 3, payload, framePayload);
    }

    return {Error::none, framePayload + 3, framePayload};
}
