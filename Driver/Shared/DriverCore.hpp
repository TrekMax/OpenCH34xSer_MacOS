#pragma once

#include <cstddef>
#include <cstdint>

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

} // namespace ch9344::driver
