#include "DriverCore.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace {

constexpr std::uint8_t kMinimumRingLogSize = 1;
constexpr std::uint8_t kMaximumRingLogSize = 30;
constexpr std::size_t kMaximumUsbPacketSize = 512;
constexpr std::size_t kMaximumTxPayloadSize = kMaximumUsbPacketSize - 3;
constexpr std::size_t kMaximumRxPayloadSize = 16 * 30;

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

struct RxCollector {
    std::uint8_t targetLogicalPort;
    std::array<std::uint8_t, kMaximumRxPayloadSize> bytes;
    std::size_t length;
    bool overflow;
};

void collectRx(void* context, const ch9344::RxRecordView& record)
{
    auto* collector = static_cast<RxCollector*>(context);
    if (record.logicalPort != collector->targetLogicalPort) {
        return;
    }
    if (record.payloadLength > collector->bytes.size() - collector->length) {
        collector->overflow = true;
        return;
    }
    std::memcpy(
        collector->bytes.data() + collector->length,
        record.payload,
        record.payloadLength);
    collector->length += record.payloadLength;
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

ch9344::driver::DriverCoreError ch9344::driver::buildUartConfiguration(
    ch9344::ChipVariant variant,
    std::uint8_t logicalPort,
    std::uint32_t baudRate,
    std::uint8_t dataBits,
    std::uint8_t halfStopBits,
    std::uint8_t parity,
    ch9344::CommandSequence* output)
{
    if (output == nullptr) {
        return DriverCoreError::invalidArgument;
    }
    if (dataBits != 8 || halfStopBits != kOneStopBitInHalfBits ||
        parity != kParityNone) {
        return DriverCoreError::unsupportedLineCoding;
    }

    return ch9344::encodeUart8N1(variant, logicalPort, baudRate, output) ==
            ch9344::Error::none
        ? DriverCoreError::none
        : DriverCoreError::protocolError;
}

ch9344::driver::DriverCoreError ch9344::driver::buildModemConfiguration(
    std::uint8_t logicalPort,
    bool dtr,
    bool rts,
    ch9344::CommandSequence* output)
{
    if (output == nullptr) {
        return DriverCoreError::invalidArgument;
    }
    return ch9344::encodeModemControl(logicalPort, dtr, rts, output) ==
            ch9344::Error::none
        ? DriverCoreError::none
        : DriverCoreError::protocolError;
}

ch9344::driver::TxPumpResult ch9344::driver::prepareTxTransfer(
    std::uint8_t logicalPort,
    const std::uint8_t* txRing,
    std::uint8_t txLogSize,
    std::uint32_t producerIndex,
    std::uint32_t consumerIndex,
    std::size_t maxPacketSize,
    std::uint8_t* output,
    std::size_t outputCapacity)
{
    if (output == nullptr) {
        return {DriverCoreError::invalidArgument, 0, 0, consumerIndex};
    }
    if (maxPacketSize <= 3 || maxPacketSize > kMaximumUsbPacketSize) {
        return {DriverCoreError::protocolError, 0, 0, consumerIndex};
    }

    std::array<std::uint8_t, kMaximumTxPayloadSize> payload {};
    const RingReadResult read = peekTxRing(
        txRing,
        txLogSize,
        producerIndex,
        consumerIndex,
        payload.data(),
        std::min(payload.size(), maxPacketSize - 3),
        maxPacketSize - 3);
    if (read.error != RingError::none) {
        return {
            read.error == RingError::invalidArgument
                ? DriverCoreError::invalidArgument
                : DriverCoreError::invalidRing,
            0,
            0,
            consumerIndex,
        };
    }
    if (read.bytesCopied == 0) {
        return {DriverCoreError::none, 0, 0, consumerIndex};
    }

    const ch9344::TxFrameResult frame = ch9344::encodeTxFrame(
        logicalPort,
        payload.data(),
        read.bytesCopied,
        maxPacketSize,
        output,
        outputCapacity);
    if (frame.error != ch9344::Error::none) {
        return {DriverCoreError::protocolError, 0, 0, consumerIndex};
    }
    return {
        DriverCoreError::none,
        frame.outputLength,
        frame.payloadConsumed,
        read.nextConsumerIndex,
    };
}

ch9344::driver::TxCompletionResult ch9344::driver::completeTxTransfer(
    bool active,
    bool transferSucceeded,
    std::size_t expectedLength,
    std::size_t actualLength,
    std::uint32_t currentConsumerIndex,
    std::uint32_t candidateConsumerIndex)
{
    const bool committed = active && transferSucceeded &&
        expectedLength != 0 && actualLength == expectedLength;
    return {
        committed,
        committed ? candidateConsumerIndex : currentConsumerIndex,
    };
}

ch9344::driver::RxPumpResult ch9344::driver::deliverRxTransfer(
    std::uint8_t targetLogicalPort,
    const std::uint8_t* transfer,
    std::size_t transferLength,
    std::uint8_t* rxRing,
    std::uint8_t rxLogSize,
    std::uint32_t producerIndex,
    std::uint32_t consumerIndex)
{
    if (targetLogicalPort >= 4 || transferLength > kMaximumUsbPacketSize) {
        return {DriverCoreError::invalidArgument, 0, producerIndex};
    }

    std::uint32_t ringSize = 0;
    const RingError ringError = validateRing(
        rxRing,
        rxLogSize,
        producerIndex,
        consumerIndex,
        &ringSize);
    if (ringError != RingError::none) {
        return {
            ringError == RingError::invalidArgument
                ? DriverCoreError::invalidArgument
                : DriverCoreError::invalidRing,
            0,
            producerIndex,
        };
    }

    RxCollector collector {targetLogicalPort, {}, 0, false};
    const ch9344::DecodeResult decode = ch9344::decodeRxTransfer(
        transfer,
        transferLength,
        collectRx,
        &collector);
    if (decode.error != ch9344::Error::none || collector.overflow) {
        return {DriverCoreError::protocolError, 0, producerIndex};
    }

    const std::uint32_t mask = ringSize - 1;
    const std::size_t freeSpace = (consumerIndex - producerIndex - 1) & mask;
    if (collector.length > freeSpace) {
        return {DriverCoreError::rxBackpressure, 0, producerIndex};
    }
    if (collector.length == 0) {
        return {DriverCoreError::none, 0, producerIndex};
    }

    const RingWriteResult write = writeRxRing(
        rxRing,
        rxLogSize,
        producerIndex,
        consumerIndex,
        collector.bytes.data(),
        collector.length);
    if (write.error != RingError::none || write.bytesCopied != collector.length) {
        return {DriverCoreError::invalidRing, 0, producerIndex};
    }
    return {
        DriverCoreError::none,
        write.bytesCopied,
        write.nextProducerIndex,
    };
}
