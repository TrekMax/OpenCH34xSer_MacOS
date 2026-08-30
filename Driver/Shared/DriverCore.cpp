#include "DriverCore.hpp"

#include <algorithm>
#include <cstring>

namespace {

constexpr std::uint8_t kMinimumRingLogSize = 1;
constexpr std::uint8_t kMaximumRingLogSize = 30;

ch9344::driver::RingError validateRing(
    const void* ring,
    std::uint8_t logSize,
    std::uint32_t producerIndex,
    std::uint32_t consumerIndex,
    std::uint32_t* ringSize)
{
    if (ring == nullptr || ringSize == nullptr) {
        return ch9344::driver::RingError::invalidArgument;
    }
    if (logSize < kMinimumRingLogSize || logSize > kMaximumRingLogSize) {
        return ch9344::driver::RingError::invalidLogSize;
    }

    const std::uint32_t size = std::uint32_t {1} << logSize;
    if (producerIndex >= size || consumerIndex >= size) {
        return ch9344::driver::RingError::invalidIndex;
    }

    *ringSize = size;
    return ch9344::driver::RingError::none;
}

} // namespace

ch9344::driver::RingReadResult ch9344::driver::peekTxRing(
    const std::uint8_t* ring,
    std::uint8_t logSize,
    std::uint32_t producerIndex,
    std::uint32_t consumerIndex,
    std::uint8_t* output,
    std::size_t outputCapacity,
    std::size_t maxBytes)
{
    std::uint32_t ringSize = 0;
    const auto validation = validateRing(
        ring, logSize, producerIndex, consumerIndex, &ringSize);
    if (validation != RingError::none) {
        return {validation, 0, consumerIndex};
    }
    if (output == nullptr) {
        return {RingError::invalidArgument, 0, consumerIndex};
    }

    const std::uint32_t mask = ringSize - 1;
    const std::size_t available = (producerIndex - consumerIndex) & mask;
    const std::size_t bytesToCopy = std::min({available, outputCapacity, maxBytes});
    const std::size_t firstLength = std::min<std::size_t>(
        bytesToCopy, ringSize - consumerIndex);
    if (firstLength > 0) {
        std::memcpy(output, ring + consumerIndex, firstLength);
    }
    const std::size_t secondLength = bytesToCopy - firstLength;
    if (secondLength > 0) {
        std::memcpy(output + firstLength, ring, secondLength);
    }

    return {
        RingError::none,
        bytesToCopy,
        static_cast<std::uint32_t>((consumerIndex + bytesToCopy) & mask),
    };
}

ch9344::driver::RingWriteResult ch9344::driver::writeRxRing(
    std::uint8_t* ring,
    std::uint8_t logSize,
    std::uint32_t producerIndex,
    std::uint32_t consumerIndex,
    const std::uint8_t* input,
    std::size_t inputLength)
{
    std::uint32_t ringSize = 0;
    const auto validation = validateRing(
        ring, logSize, producerIndex, consumerIndex, &ringSize);
    if (validation != RingError::none) {
        return {validation, 0, producerIndex};
    }
    if (input == nullptr) {
        return {RingError::invalidArgument, 0, producerIndex};
    }

    const std::uint32_t mask = ringSize - 1;
    const std::size_t freeSpace = (consumerIndex - producerIndex - 1) & mask;
    const std::size_t bytesToCopy = std::min(inputLength, freeSpace);
    const std::size_t firstLength = std::min<std::size_t>(
        bytesToCopy, ringSize - producerIndex);
    if (firstLength > 0) {
        std::memcpy(ring + producerIndex, input, firstLength);
    }
    const std::size_t secondLength = bytesToCopy - firstLength;
    if (secondLength > 0) {
        std::memcpy(ring, input + firstLength, secondLength);
    }

    return {
        RingError::none,
        bytesToCopy,
        static_cast<std::uint32_t>((producerIndex + bytesToCopy) & mask),
    };
}
