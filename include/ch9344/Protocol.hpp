#pragma once

#include <cstddef>
#include <cstdint>

namespace ch9344 {

enum class Error {
    none,
    invalidArgument,
    invalidPort,
    invalidPacketSize,
    outputTooSmall,
};

Error mapLogicalPort(uint8_t logicalPort, uint8_t* hardwarePort);

struct TxFrameResult {
    Error error;
    std::size_t outputLength;
    std::size_t payloadConsumed;
};

TxFrameResult encodeTxFrame(
    uint8_t logicalPort,
    const uint8_t* payload,
    std::size_t payloadLength,
    std::size_t maxPacketSize,
    uint8_t* output,
    std::size_t outputCapacity);

} // namespace ch9344
