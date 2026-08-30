#include "../TestSupport.hpp"

#include <ch9344/Protocol.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>

namespace {

void testLogicalPortMapping()
{
    const uint8_t expected[] = {4, 5, 6, 7};

    for (uint8_t logical = 0; logical < 4; ++logical) {
        uint8_t hardware = 0;
        CHECK_EQ(
            ch9344::mapLogicalPort(logical, &hardware),
            ch9344::Error::none);
        CHECK_EQ(hardware, expected[logical]);
    }

    uint8_t untouched = 0xa5;
    CHECK_EQ(
        ch9344::mapLogicalPort(4, &untouched),
        ch9344::Error::invalidPort);
    CHECK_EQ(untouched, 0xa5);
    CHECK_EQ(
        ch9344::mapLogicalPort(0, nullptr),
        ch9344::Error::invalidArgument);
}

void testTxFrameEncoding()
{
    const uint8_t payload[] = {0x00, 0x7f, 0xff};
    uint8_t output[64] = {};

    const auto result = ch9344::encodeTxFrame(
        3, payload, sizeof(payload), 64, output, sizeof(output));
    const uint8_t expected[] = {0x07, 0x03, 0x00, 0x00, 0x7f, 0xff};

    CHECK_EQ(result.error, ch9344::Error::none);
    CHECK_EQ(result.outputLength, sizeof(expected));
    CHECK_EQ(result.payloadConsumed, sizeof(payload));
    CHECK_BYTES(output, expected, sizeof(expected));
}

void testTxFrameStopsAtPacketBoundary()
{
    uint8_t payload[70] = {};
    for (std::size_t index = 0; index < sizeof(payload); ++index) {
        payload[index] = static_cast<uint8_t>(index + 1);
    }
    uint8_t output[64] = {};

    const auto result = ch9344::encodeTxFrame(
        0, payload, sizeof(payload), 64, output, sizeof(output));

    CHECK_EQ(result.error, ch9344::Error::none);
    CHECK_EQ(result.outputLength, 64U);
    CHECK_EQ(result.payloadConsumed, 61U);
    CHECK_EQ(output[0], 0x04);
    CHECK_EQ(output[1], 0x3d);
    CHECK_EQ(output[2], 0x00);
    CHECK_BYTES(output + 3, payload, 61);
}

void testTxFrameRejectsInvalidInputWithoutWriting()
{
    const uint8_t payload[] = {0x11, 0x22, 0x33};
    uint8_t output[64];

    const auto checkRejected = [&](ch9344::TxFrameResult result, ch9344::Error expected) {
        CHECK_EQ(result.error, expected);
        CHECK_EQ(result.outputLength, 0U);
        CHECK_EQ(result.payloadConsumed, 0U);
        for (uint8_t byte : output) {
            CHECK_EQ(byte, 0xa5);
        }
    };

    std::fill(std::begin(output), std::end(output), 0xa5);
    checkRejected(
        ch9344::encodeTxFrame(4, payload, sizeof(payload), 64, output, sizeof(output)),
        ch9344::Error::invalidPort);

    std::fill(std::begin(output), std::end(output), 0xa5);
    checkRejected(
        ch9344::encodeTxFrame(0, nullptr, 1, 64, output, sizeof(output)),
        ch9344::Error::invalidArgument);

    std::fill(std::begin(output), std::end(output), 0xa5);
    const auto nullOutput = ch9344::encodeTxFrame(
        0, payload, sizeof(payload), 64, nullptr, sizeof(output));
    CHECK_EQ(nullOutput.error, ch9344::Error::invalidArgument);
    CHECK_EQ(nullOutput.outputLength, 0U);
    CHECK_EQ(nullOutput.payloadConsumed, 0U);

    std::fill(std::begin(output), std::end(output), 0xa5);
    checkRejected(
        ch9344::encodeTxFrame(0, payload, sizeof(payload), 3, output, sizeof(output)),
        ch9344::Error::invalidPacketSize);

    std::fill(std::begin(output), std::end(output), 0xa5);
    checkRejected(
        ch9344::encodeTxFrame(0, payload, sizeof(payload), 64, output, 5),
        ch9344::Error::outputTooSmall);
}

struct CapturedRecord {
    uint8_t logicalPort = 0;
    uint8_t payload[30] = {};
    std::size_t payloadLength = 0;
};

struct RxCapture {
    CapturedRecord records[4] = {};
    std::size_t count = 0;
};

void captureRxRecord(void* context, const ch9344::RxRecordView& record)
{
    auto* capture = static_cast<RxCapture*>(context);
    if (capture->count >= 4 || record.payloadLength > 30) {
        ++test_support::failures;
        return;
    }

    auto& destination = capture->records[capture->count++];
    destination.logicalPort = record.logicalPort;
    destination.payloadLength = record.payloadLength;
    if (record.payloadLength != 0) {
        std::memcpy(destination.payload, record.payload, record.payloadLength);
    }
}

void testRxDecodesOneRecord()
{
    uint8_t input[32] = {};
    input[0] = 0x07;
    input[1] = 0x03;
    input[2] = 0xa5;
    input[3] = 0x00;
    input[4] = 0xff;
    RxCapture capture;

    const auto result = ch9344::decodeRxTransfer(
        input, sizeof(input), captureRxRecord, &capture);
    const uint8_t expectedPayload[] = {0xa5, 0x00, 0xff};

    CHECK_EQ(result.error, ch9344::Error::none);
    CHECK_EQ(result.recordsDecoded, 1U);
    CHECK_EQ(result.bytesConsumed, 32U);
    CHECK_EQ(capture.count, 1U);
    CHECK_EQ(capture.records[0].logicalPort, 3);
    CHECK_EQ(capture.records[0].payloadLength, sizeof(expectedPayload));
    CHECK_BYTES(
        capture.records[0].payload,
        expectedPayload,
        sizeof(expectedPayload));
}

void testRxDemultiplexesRecordsInInputOrder()
{
    uint8_t input[64] = {};
    input[0] = 0x04;
    input[1] = 0x01;
    input[2] = 0x11;
    input[32] = 0x06;
    input[33] = 0x02;
    input[34] = 0x22;
    input[35] = 0x33;
    RxCapture capture;

    const auto result = ch9344::decodeRxTransfer(
        input, sizeof(input), captureRxRecord, &capture);
    const uint8_t firstPayload[] = {0x11};
    const uint8_t secondPayload[] = {0x22, 0x33};

    CHECK_EQ(result.error, ch9344::Error::none);
    CHECK_EQ(result.recordsDecoded, 2U);
    CHECK_EQ(result.bytesConsumed, 64U);
    CHECK_EQ(capture.count, 2U);
    CHECK_EQ(capture.records[0].logicalPort, 0);
    CHECK_BYTES(capture.records[0].payload, firstPayload, sizeof(firstPayload));
    CHECK_EQ(capture.records[1].logicalPort, 2);
    CHECK_BYTES(capture.records[1].payload, secondPayload, sizeof(secondPayload));
}

void testRxRejectsTruncatedRecord()
{
    uint8_t input[31] = {};
    input[0] = 0x04;
    RxCapture capture;

    const auto result = ch9344::decodeRxTransfer(
        input, sizeof(input), captureRxRecord, &capture);

    CHECK_EQ(result.error, ch9344::Error::truncatedRxRecord);
    CHECK_EQ(result.recordsDecoded, 0U);
    CHECK_EQ(result.bytesConsumed, 0U);
    CHECK_EQ(capture.count, 0U);
}

void testRxRejectsHardwarePortOutsideFourThroughSeven()
{
    for (const uint8_t hardwarePort : {uint8_t {3}, uint8_t {8}}) {
        uint8_t input[32] = {};
        input[0] = hardwarePort;
        RxCapture capture;

        const auto result = ch9344::decodeRxTransfer(
            input, sizeof(input), captureRxRecord, &capture);

        CHECK_EQ(result.error, ch9344::Error::invalidRxPort);
        CHECK_EQ(result.recordsDecoded, 0U);
        CHECK_EQ(result.bytesConsumed, 0U);
        CHECK_EQ(capture.count, 0U);
    }
}

void testRxRejectsPayloadLongerThanThirtyBytes()
{
    uint8_t input[32] = {};
    input[0] = 0x04;
    input[1] = 31;
    RxCapture capture;

    const auto result = ch9344::decodeRxTransfer(
        input, sizeof(input), captureRxRecord, &capture);

    CHECK_EQ(result.error, ch9344::Error::invalidRxLength);
    CHECK_EQ(result.recordsDecoded, 0U);
    CHECK_EQ(result.bytesConsumed, 0U);
    CHECK_EQ(capture.count, 0U);
}

void testRxValidatesPointersAndAcceptsEmptyInput()
{
    uint8_t input[32] = {};
    input[0] = 0x04;
    RxCapture capture;

    const auto nullInput = ch9344::decodeRxTransfer(
        nullptr, sizeof(input), captureRxRecord, &capture);
    CHECK_EQ(nullInput.error, ch9344::Error::invalidArgument);
    CHECK_EQ(capture.count, 0U);

    const auto nullHandler = ch9344::decodeRxTransfer(
        input, sizeof(input), nullptr, &capture);
    CHECK_EQ(nullHandler.error, ch9344::Error::invalidArgument);
    CHECK_EQ(capture.count, 0U);

    const auto empty = ch9344::decodeRxTransfer(
        nullptr, 0, captureRxRecord, &capture);
    CHECK_EQ(empty.error, ch9344::Error::none);
    CHECK_EQ(empty.recordsDecoded, 0U);
    CHECK_EQ(empty.bytesConsumed, 0U);
    CHECK_EQ(capture.count, 0U);
}

} // namespace

int main()
{
    testLogicalPortMapping();
    testTxFrameEncoding();
    testTxFrameStopsAtPacketBoundary();
    testTxFrameRejectsInvalidInputWithoutWriting();
    testRxDecodesOneRecord();
    testRxDemultiplexesRecordsInInputOrder();
    testRxRejectsTruncatedRecord();
    testRxRejectsHardwarePortOutsideFourThroughSeven();
    testRxRejectsPayloadLongerThanThirtyBytes();
    testRxValidatesPointersAndAcceptsEmptyInput();
    return test_support::failures == 0 ? 0 : 1;
}
