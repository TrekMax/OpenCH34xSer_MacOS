#include "../../Driver/Shared/DriverCore.hpp"
#include "../TestSupport.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

using ch9344::driver::RingError;

void testReadsContiguousTxWithoutCommittingConsumer()
{
    const std::array<std::uint8_t, 8> ring {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77}};
    std::array<std::uint8_t, 4> output {{0xa5, 0xa5, 0xa5, 0xa5}};

    const auto result = ch9344::driver::peekTxRing(
        ring.data(), 3, 5, 1, output.data(), output.size(), 3);
    const std::uint8_t expected[] = {0x11, 0x22, 0x33};

    CHECK_EQ(result.error, RingError::none);
    CHECK_EQ(result.bytesCopied, 3U);
    CHECK_EQ(result.nextConsumerIndex, 4U);
    CHECK_BYTES(output.data(), expected, sizeof(expected));
    CHECK_EQ(output[3], 0xa5);
}

void testReadsWrappedTxAndHonorsOutputCapacity()
{
    const std::array<std::uint8_t, 8> ring {{0xa0, 0xa1, 0x02, 0x03, 0x04, 0x05, 0xa6, 0xa7}};
    std::array<std::uint8_t, 3> output {};

    const auto result = ch9344::driver::peekTxRing(
        ring.data(), 3, 2, 6, output.data(), output.size(), 7);
    const std::uint8_t expected[] = {0xa6, 0xa7, 0xa0};

    CHECK_EQ(result.error, RingError::none);
    CHECK_EQ(result.bytesCopied, 3U);
    CHECK_EQ(result.nextConsumerIndex, 1U);
    CHECK_BYTES(output.data(), expected, sizeof(expected));
}

void testReadsEmptyTxWithoutTouchingOutput()
{
    const std::array<std::uint8_t, 8> ring {};
    std::array<std::uint8_t, 2> output {{0xa5, 0x5a}};

    const auto result = ch9344::driver::peekTxRing(
        ring.data(), 3, 4, 4, output.data(), output.size(), output.size());

    CHECK_EQ(result.error, RingError::none);
    CHECK_EQ(result.bytesCopied, 0U);
    CHECK_EQ(result.nextConsumerIndex, 4U);
    CHECK_EQ(output[0], 0xa5);
    CHECK_EQ(output[1], 0x5a);
}

void testWritesContiguousAndWrappedRx()
{
    std::array<std::uint8_t, 8> contiguous {};
    const std::uint8_t first[] = {0x11, 0x22};
    const auto firstResult = ch9344::driver::writeRxRing(
        contiguous.data(), 3, 1, 5, first, sizeof(first));

    CHECK_EQ(firstResult.error, RingError::none);
    CHECK_EQ(firstResult.bytesCopied, sizeof(first));
    CHECK_EQ(firstResult.nextProducerIndex, 3U);
    CHECK_EQ(contiguous[1], 0x11);
    CHECK_EQ(contiguous[2], 0x22);

    std::array<std::uint8_t, 8> wrapped {};
    const std::uint8_t second[] = {0xa7, 0xa0, 0xa1};
    const auto secondResult = ch9344::driver::writeRxRing(
        wrapped.data(), 3, 7, 3, second, sizeof(second));

    CHECK_EQ(secondResult.error, RingError::none);
    CHECK_EQ(secondResult.bytesCopied, sizeof(second));
    CHECK_EQ(secondResult.nextProducerIndex, 2U);
    CHECK_EQ(wrapped[7], 0xa7);
    CHECK_EQ(wrapped[0], 0xa0);
    CHECK_EQ(wrapped[1], 0xa1);
}

void testRxPreservesOneSlotAndReportsPartialWrite()
{
    std::array<std::uint8_t, 8> ring {{0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5}};
    const std::uint8_t input[] = {1, 2, 3, 4, 5};

    const auto partial = ch9344::driver::writeRxRing(
        ring.data(), 3, 2, 5, input, sizeof(input));
    CHECK_EQ(partial.error, RingError::none);
    CHECK_EQ(partial.bytesCopied, 2U);
    CHECK_EQ(partial.nextProducerIndex, 4U);
    CHECK_EQ(ring[2], 1);
    CHECK_EQ(ring[3], 2);
    CHECK_EQ(ring[4], 0xa5);

    const auto full = ch9344::driver::writeRxRing(
        ring.data(), 3, 4, 5, input, sizeof(input));
    CHECK_EQ(full.error, RingError::none);
    CHECK_EQ(full.bytesCopied, 0U);
    CHECK_EQ(full.nextProducerIndex, 4U);
}

void testRejectsInvalidRingMetadataWithoutWriting()
{
    std::array<std::uint8_t, 8> ring {{0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5}};
    std::array<std::uint8_t, 2> output {{0x5a, 0x5a}};
    const std::uint8_t input[] = {1, 2};

    CHECK_EQ(
        ch9344::driver::peekTxRing(
            ring.data(), 0, 1, 0, output.data(), output.size(), output.size()).error,
        RingError::invalidLogSize);
    CHECK_EQ(
        ch9344::driver::peekTxRing(
            ring.data(), 31, 1, 0, output.data(), output.size(), output.size()).error,
        RingError::invalidLogSize);
    CHECK_EQ(
        ch9344::driver::peekTxRing(
            ring.data(), 3, 8, 0, output.data(), output.size(), output.size()).error,
        RingError::invalidIndex);
    CHECK_EQ(
        ch9344::driver::peekTxRing(
            nullptr, 3, 1, 0, output.data(), output.size(), output.size()).error,
        RingError::invalidArgument);
    CHECK_EQ(
        ch9344::driver::peekTxRing(
            ring.data(), 3, 1, 0, nullptr, 1, 1).error,
        RingError::invalidArgument);

    CHECK_EQ(
        ch9344::driver::writeRxRing(
            ring.data(), 3, 8, 0, input, sizeof(input)).error,
        RingError::invalidIndex);
    CHECK_EQ(
        ch9344::driver::writeRxRing(
            ring.data(), 3, 1, 0, nullptr, sizeof(input)).error,
        RingError::invalidArgument);
    CHECK_EQ(ring[0], 0xa5);
    CHECK_EQ(output[0], 0x5a);
}

void testBuildsQChipPort4EightNOneConfiguration()
{
    ch9344::CommandSequence output;
    const std::uint8_t expectedBaud[] = {
        0x20, 0x3b, 0x00, 0x00, 0x00, 0x00, 0xc2, 0x01, 0x00,
    };

    CHECK_EQ(
        ch9344::driver::buildUartConfiguration(
            ch9344::ChipVariant::ch9344Q,
            3,
            115200,
            8,
            ch9344::driver::kOneStopBitInHalfBits,
            ch9344::driver::kParityNone,
            &output),
        ch9344::driver::DriverCoreError::none);
    CHECK_EQ(output.count, 6U);
    CHECK_EQ(output.commands[1].length, sizeof(expectedBaud));
    CHECK_BYTES(output.commands[1].bytes, expectedBaud, sizeof(expectedBaud));
}

void testRejectsUnsupportedLineCodingAndProtocolErrorsWithoutWriting()
{
    ch9344::CommandSequence output;
    output.count = 0xa5;
    output.commands[0].bytes[0] = 0x5a;

    CHECK_EQ(
        ch9344::driver::buildUartConfiguration(
            ch9344::ChipVariant::ch9344Q, 3, 115200, 7, 2, 1, &output),
        ch9344::driver::DriverCoreError::unsupportedLineCoding);
    CHECK_EQ(
        ch9344::driver::buildUartConfiguration(
            ch9344::ChipVariant::ch9344Q, 3, 115200, 8, 4, 1, &output),
        ch9344::driver::DriverCoreError::unsupportedLineCoding);
    CHECK_EQ(
        ch9344::driver::buildUartConfiguration(
            ch9344::ChipVariant::ch9344Q, 3, 115200, 8, 2, 2, &output),
        ch9344::driver::DriverCoreError::unsupportedLineCoding);
    CHECK_EQ(
        ch9344::driver::buildUartConfiguration(
            ch9344::ChipVariant::ch9344Q, 3, 0, 8, 2, 1, &output),
        ch9344::driver::DriverCoreError::protocolError);
    CHECK_EQ(
        ch9344::driver::buildUartConfiguration(
            ch9344::ChipVariant::ch9344Q, 3, 115200, 8, 2, 1, nullptr),
        ch9344::driver::DriverCoreError::invalidArgument);
    CHECK_EQ(output.count, 0xa5U);
    CHECK_EQ(output.commands[0].bytes[0], 0x5a);
}

void testBuildsPort4DtrRtsConfiguration()
{
    ch9344::CommandSequence output;
    const std::uint8_t expectedDtr[] = {0x80, 0x3c, 0x01};
    const std::uint8_t expectedRts[] = {0x80, 0x3c, 0x10};

    CHECK_EQ(
        ch9344::driver::buildModemConfiguration(3, true, false, &output),
        ch9344::driver::DriverCoreError::none);
    CHECK_EQ(output.count, 2U);
    CHECK_BYTES(output.commands[0].bytes, expectedDtr, sizeof(expectedDtr));
    CHECK_BYTES(output.commands[1].bytes, expectedRts, sizeof(expectedRts));

    CHECK_EQ(
        ch9344::driver::buildModemConfiguration(3, true, false, nullptr),
        ch9344::driver::DriverCoreError::invalidArgument);
}

void testPreparesWrappedPort4TxWithoutCommittingRingIndex()
{
    const std::array<std::uint8_t, 8> ring {{0xa0, 0xa1, 0x02, 0x03, 0x04, 0x05, 0xa6, 0xa7}};
    std::array<std::uint8_t, 512> output {};
    const std::uint8_t expected[] = {0x07, 0x04, 0x00, 0xa6, 0xa7, 0xa0, 0xa1};

    const auto result = ch9344::driver::prepareTxTransfer(
        3,
        ring.data(),
        3,
        2,
        6,
        512,
        output.data(),
        output.size());

    CHECK_EQ(result.error, ch9344::driver::DriverCoreError::none);
    CHECK_EQ(result.frameLength, sizeof(expected));
    CHECK_EQ(result.payloadLength, 4U);
    CHECK_EQ(result.nextConsumerIndex, 2U);
    CHECK_BYTES(output.data(), expected, sizeof(expected));
}

void testEmptyTxDoesNotCreateUsbFrame()
{
    const std::array<std::uint8_t, 8> ring {};
    std::array<std::uint8_t, 512> output {{0xa5}};
    const auto result = ch9344::driver::prepareTxTransfer(
        3, ring.data(), 3, 4, 4, 512, output.data(), output.size());

    CHECK_EQ(result.error, ch9344::driver::DriverCoreError::none);
    CHECK_EQ(result.frameLength, 0U);
    CHECK_EQ(result.payloadLength, 0U);
    CHECK_EQ(result.nextConsumerIndex, 4U);
    CHECK_EQ(output[0], 0xa5);
}

void testCommitsTxOnlyAfterCompleteActiveUsbWrite()
{
    const auto complete = ch9344::driver::completeTxTransfer(
        true, true, 12, 12, 3, 7);
    CHECK_EQ(complete.committed, true);
    CHECK_EQ(complete.nextConsumerIndex, 7U);

    const auto shortWrite = ch9344::driver::completeTxTransfer(
        true, true, 12, 11, 3, 7);
    CHECK_EQ(shortWrite.committed, false);
    CHECK_EQ(shortWrite.nextConsumerIndex, 3U);

    const auto usbError = ch9344::driver::completeTxTransfer(
        true, false, 12, 12, 3, 7);
    CHECK_EQ(usbError.committed, false);
    CHECK_EQ(usbError.nextConsumerIndex, 3U);

    const auto stopped = ch9344::driver::completeTxTransfer(
        false, true, 12, 12, 3, 7);
    CHECK_EQ(stopped.committed, false);
    CHECK_EQ(stopped.nextConsumerIndex, 3U);
}

void testDeliversOnlyPort4RxAcrossRingWrap()
{
    std::array<std::uint8_t, 64> transfer {};
    transfer[0] = 0x04;
    transfer[1] = 0x02;
    transfer[2] = 0x11;
    transfer[3] = 0x22;
    transfer[32] = 0x07;
    transfer[33] = 0x03;
    transfer[34] = 0xa7;
    transfer[35] = 0xa0;
    transfer[36] = 0xa1;
    std::array<std::uint8_t, 8> ring {};

    const auto result = ch9344::driver::deliverRxTransfer(
        3,
        transfer.data(),
        transfer.size(),
        ring.data(),
        3,
        7,
        4);

    CHECK_EQ(result.error, ch9344::driver::DriverCoreError::none);
    CHECK_EQ(result.bytesWritten, 3U);
    CHECK_EQ(result.nextProducerIndex, 2U);
    CHECK_EQ(ring[7], 0xa7);
    CHECK_EQ(ring[0], 0xa0);
    CHECK_EQ(ring[1], 0xa1);
}

void testRxBackpressureAndMalformedTransferDoNotModifyRing()
{
    std::array<std::uint8_t, 32> transfer {};
    transfer[0] = 0x07;
    transfer[1] = 0x03;
    transfer[2] = 1;
    transfer[3] = 2;
    transfer[4] = 3;
    std::array<std::uint8_t, 8> ring {{0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5}};

    const auto backpressure = ch9344::driver::deliverRxTransfer(
        3,
        transfer.data(),
        transfer.size(),
        ring.data(),
        3,
        2,
        4);
    CHECK_EQ(
        backpressure.error,
        ch9344::driver::DriverCoreError::rxBackpressure);
    CHECK_EQ(backpressure.bytesWritten, 0U);
    CHECK_EQ(backpressure.nextProducerIndex, 2U);
    CHECK_EQ(ring[2], 0xa5);

    transfer[1] = 31;
    const auto malformed = ch9344::driver::deliverRxTransfer(
        3,
        transfer.data(),
        transfer.size(),
        ring.data(),
        3,
        7,
        4);
    CHECK_EQ(malformed.error, ch9344::driver::DriverCoreError::protocolError);
    CHECK_EQ(malformed.bytesWritten, 0U);
    CHECK_EQ(malformed.nextProducerIndex, 7U);
    CHECK_EQ(ring[7], 0xa5);
}

} // namespace

int main()
{
    testReadsContiguousTxWithoutCommittingConsumer();
    testReadsWrappedTxAndHonorsOutputCapacity();
    testReadsEmptyTxWithoutTouchingOutput();
    testWritesContiguousAndWrappedRx();
    testRxPreservesOneSlotAndReportsPartialWrite();
    testRejectsInvalidRingMetadataWithoutWriting();
    testBuildsQChipPort4EightNOneConfiguration();
    testRejectsUnsupportedLineCodingAndProtocolErrorsWithoutWriting();
    testBuildsPort4DtrRtsConfiguration();
    testPreparesWrappedPort4TxWithoutCommittingRingIndex();
    testEmptyTxDoesNotCreateUsbFrame();
    testCommitsTxOnlyAfterCompleteActiveUsbWrite();
    testDeliversOnlyPort4RxAcrossRingWrap();
    testRxBackpressureAndMalformedTransferDoNotModifyRing();
    return test_support::failures == 0 ? 0 : 1;
}
