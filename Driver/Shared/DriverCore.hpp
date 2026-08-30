#pragma once

#include <cstddef>
#include <cstdint>

#include <ch9344/Protocol.hpp>

namespace ch9344::driver {

enum class RingError {
    none,
    invalidArgument,
    invalidLogSize,
    invalidIndex,
};

struct RingReadResult {
    RingError error;
    std::size_t bytesCopied;
    std::uint32_t nextConsumerIndex;
};

struct RingWriteResult {
    RingError error;
    std::size_t bytesCopied;
    std::uint32_t nextProducerIndex;
};

enum class DriverCoreError {
    none,
    invalidArgument,
    unsupportedLineCoding,
    protocolError,
};

constexpr std::uint8_t kOneStopBitInHalfBits = 2;
constexpr std::uint8_t kParityNone = 1;

RingReadResult peekTxRing(
    const std::uint8_t* ring,
    std::uint8_t logSize,
    std::uint32_t producerIndex,
    std::uint32_t consumerIndex,
    std::uint8_t* output,
    std::size_t outputCapacity,
    std::size_t maxBytes);

RingWriteResult writeRxRing(
    std::uint8_t* ring,
    std::uint8_t logSize,
    std::uint32_t producerIndex,
    std::uint32_t consumerIndex,
    const std::uint8_t* input,
    std::size_t inputLength);

DriverCoreError buildUartConfiguration(
    ch9344::ChipVariant variant,
    std::uint8_t logicalPort,
    std::uint32_t baudRate,
    std::uint8_t dataBits,
    std::uint8_t halfStopBits,
    std::uint8_t parity,
    ch9344::CommandSequence* output);

DriverCoreError buildModemConfiguration(
    std::uint8_t logicalPort,
    bool dtr,
    bool rts,
    ch9344::CommandSequence* output);

} // namespace ch9344::driver
