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

void testChipVersionDistinguishesLAndQ()
{
    const uint8_t lResponse[] = {0x3f, 0x11, 0x22, 0x33};
    const uint8_t qResponse[] = {0x40, 0xaa, 0xbb, 0xcc};
    ch9344::ChipInfo info {ch9344::ChipVariant::ch9344Q, 0};

    CHECK_EQ(
        ch9344::parseChipVersion(lResponse, sizeof(lResponse), &info),
        ch9344::Error::none);
    CHECK_EQ(info.variant, ch9344::ChipVariant::ch9344L);
    CHECK_EQ(info.version, 0x3f);

    CHECK_EQ(
        ch9344::parseChipVersion(qResponse, sizeof(qResponse), &info),
        ch9344::Error::none);
    CHECK_EQ(info.variant, ch9344::ChipVariant::ch9344Q);
    CHECK_EQ(info.version, 0x40);
}

void testChipVersionRejectsMalformedResponse()
{
    const uint8_t response[] = {0x40, 0x11, 0x22, 0x33, 0x44};
    ch9344::ChipInfo info {ch9344::ChipVariant::ch9344L, 0xa5};

    CHECK_EQ(
        ch9344::parseChipVersion(response, 3, &info),
        ch9344::Error::invalidVersionResponse);
    CHECK_EQ(info.variant, ch9344::ChipVariant::ch9344L);
    CHECK_EQ(info.version, 0xa5);

    CHECK_EQ(
        ch9344::parseChipVersion(response, 5, &info),
        ch9344::Error::invalidVersionResponse);
    CHECK_EQ(
        ch9344::parseChipVersion(nullptr, 4, &info),
        ch9344::Error::invalidArgument);
    CHECK_EQ(
        ch9344::parseChipVersion(response, 4, nullptr),
        ch9344::Error::invalidArgument);
}

void testUploadModeCommandForSupportedChipVersions()
{
    const uint8_t expected[] = {0x94, 0x9d, 0x01, 0, 0, 0, 0, 0};

    for (const ch9344::ChipInfo chip : {
             ch9344::ChipInfo {ch9344::ChipVariant::ch9344Q, 0x40},
             ch9344::ChipInfo {ch9344::ChipVariant::ch9344L, 0x39}}) {
        ch9344::CommandSequence output;

        CHECK_EQ(
            ch9344::encodeDeviceInitialization(chip, &output),
            ch9344::Error::none);
        CHECK_EQ(output.count, 1U);
        CHECK_EQ(output.commands[0].length, sizeof(expected));
        CHECK_BYTES(output.commands[0].bytes, expected, sizeof(expected));
    }
}

void testOldLChipDoesNotRequireUploadModeCommand()
{
    ch9344::CommandSequence output;
    const ch9344::ChipInfo chip {ch9344::ChipVariant::ch9344L, 0x38};

    CHECK_EQ(
        ch9344::encodeDeviceInitialization(chip, &output),
        ch9344::Error::none);
    CHECK_EQ(output.count, 0U);
    CHECK_EQ(
        ch9344::encodeDeviceInitialization(chip, nullptr),
        ch9344::Error::invalidArgument);
}

void checkCommand(
    const ch9344::Command& command,
    const uint8_t* expected,
    std::size_t expectedLength)
{
    CHECK_EQ(command.length, expectedLength);
    CHECK_BYTES(command.bytes, expected, expectedLength);
}

void testPortInitializationUsesPerPortRegisterBase()
{
    const uint8_t portThreeExpected[][3] = {
        {0xc0, 0x3a, 0x87},
        {0xc0, 0x3b, 0x03},
        {0xc0, 0x3c, 0x08},
    };
    const uint8_t portZeroExpected[][3] = {
        {0xc0, 0x0a, 0x87},
        {0xc0, 0x0b, 0x03},
        {0xc0, 0x0c, 0x08},
    };

    ch9344::CommandSequence output;
    CHECK_EQ(
        ch9344::encodePortInitialization(3, &output),
        ch9344::Error::none);
    CHECK_EQ(output.count, 3U);
    for (std::size_t index = 0; index < output.count; ++index) {
        checkCommand(output.commands[index], portThreeExpected[index], 3);
    }

    CHECK_EQ(
        ch9344::encodePortInitialization(0, &output),
        ch9344::Error::none);
    CHECK_EQ(output.count, 3U);
    for (std::size_t index = 0; index < output.count; ++index) {
        checkCommand(output.commands[index], portZeroExpected[index], 3);
    }
}

void testPortInitializationRejectsInvalidInputWithoutWriting()
{
    ch9344::CommandSequence output;
    output.count = 0xa5;
    output.commands[0].bytes[0] = 0x5a;

    CHECK_EQ(
        ch9344::encodePortInitialization(4, &output),
        ch9344::Error::invalidPort);
    CHECK_EQ(output.count, 0xa5U);
    CHECK_EQ(output.commands[0].bytes[0], 0x5a);
    CHECK_EQ(
        ch9344::encodePortInitialization(0, nullptr),
        ch9344::Error::invalidArgument);
}

void testLChipEncodes115200EightNOne()
{
    const uint8_t expected[][9] = {
        {0x80, 0x39, 0x50},
        {0x20, 0x3b, 0x01, 0x00, 0x00, 0x00},
        {0xc0, 0x3b, 0x03},
        {0x97, 0x9c, 0x07, 0x02},
        {0xc0, 0x39, 0x0f},
        {0x90, 0x85, 0x3e},
    };
    const std::size_t lengths[] = {3, 6, 3, 4, 3, 3};
    ch9344::CommandSequence output;

    CHECK_EQ(
        ch9344::encodeUart8N1(
            ch9344::ChipVariant::ch9344L, 3, 115200, &output),
        ch9344::Error::none);
    CHECK_EQ(output.count, 6U);
    for (std::size_t index = 0; index < output.count; ++index) {
        checkCommand(output.commands[index], expected[index], lengths[index]);
    }
}

void testLChipEncodesClockDivisorAndReceiveTimeout()
{
    struct Case {
        uint32_t baudRate;
        uint8_t clockSelector;
        uint8_t divisorLow;
        uint8_t divisorHigh;
        uint8_t baudSelector;
        uint8_t receiveTimeout;
    };
    const Case cases[] = {
        {9600, 0x50, 0x0c, 0x00, 0x00, 0x10},
        {921600, 0x51, 0x03, 0x00, 0x00, 0x05},
        {250000, 0x51, 0x0b, 0x00, 0x01, 0x01},
        {500000, 0x51, 0x06, 0x00, 0x02, 0x01},
        {1000000, 0x51, 0x03, 0x00, 0x03, 0x05},
        {1500000, 0x51, 0x02, 0x00, 0x04, 0x05},
        {2000000, 0x51, 0x02, 0x00, 0x00, 0x05},
        {3000000, 0x51, 0x01, 0x00, 0x05, 0x05},
        {12000000, 0x51, 0x00, 0x00, 0x06, 0x05},
    };

    for (const Case& testCase : cases) {
        ch9344::CommandSequence output;
        CHECK_EQ(
            ch9344::encodeUart8N1(
                ch9344::ChipVariant::ch9344L,
                3,
                testCase.baudRate,
                &output),
            ch9344::Error::none);
        CHECK_EQ(output.count, 6U);
        CHECK_EQ(output.commands[0].bytes[2], testCase.clockSelector);
        CHECK_EQ(output.commands[1].length, 6U);
        CHECK_EQ(output.commands[1].bytes[2], testCase.divisorLow);
        CHECK_EQ(output.commands[1].bytes[3], testCase.divisorHigh);
        CHECK_EQ(output.commands[1].bytes[4], testCase.baudSelector);
        CHECK_EQ(output.commands[3].bytes[3], testCase.receiveTimeout);
    }
}

void testQChipEncodesBaudRateDirectlyAsLittleEndian()
{
    const uint8_t expectedBaudCommand[] = {
        0x20, 0x3b, 0x00, 0x00, 0x00, 0x00, 0xc2, 0x01, 0x00,
    };
    ch9344::CommandSequence output;

    CHECK_EQ(
        ch9344::encodeUart8N1(
            ch9344::ChipVariant::ch9344Q, 3, 115200, &output),
        ch9344::Error::none);
    CHECK_EQ(output.count, 6U);
    checkCommand(
        output.commands[1],
        expectedBaudCommand,
        sizeof(expectedBaudCommand));
}

void testUartEncodingRejectsInvalidInputWithoutWriting()
{
    ch9344::CommandSequence output;
    output.count = 0xa5;

    CHECK_EQ(
        ch9344::encodeUart8N1(
            ch9344::ChipVariant::ch9344L, 4, 115200, &output),
        ch9344::Error::invalidPort);
    CHECK_EQ(output.count, 0xa5U);
    CHECK_EQ(
        ch9344::encodeUart8N1(
            ch9344::ChipVariant::ch9344L, 0, 0, &output),
        ch9344::Error::invalidBaudRate);
    CHECK_EQ(output.count, 0xa5U);
    CHECK_EQ(
        ch9344::encodeUart8N1(
            ch9344::ChipVariant::ch9344L, 0, 12000001, &output),
        ch9344::Error::invalidBaudRate);
    CHECK_EQ(output.count, 0xa5U);
    CHECK_EQ(
        ch9344::encodeUart8N1(
            ch9344::ChipVariant::ch9344L, 0, 115200, nullptr),
        ch9344::Error::invalidArgument);
}

void testModemControlEncodesDtrAndRtsIndependently()
{
    struct Case {
        bool dtr;
        bool rts;
        uint8_t dtrValue;
        uint8_t rtsValue;
    };
    const Case cases[] = {
        {false, false, 0x00, 0x10},
        {true, false, 0x01, 0x10},
        {false, true, 0x00, 0x11},
        {true, true, 0x01, 0x11},
    };

    for (const Case& testCase : cases) {
        ch9344::CommandSequence output;
        CHECK_EQ(
            ch9344::encodeModemControl(
                3, testCase.dtr, testCase.rts, &output),
            ch9344::Error::none);
        CHECK_EQ(output.count, 2U);
        const uint8_t expectedDtr[] = {0x80, 0x3c, testCase.dtrValue};
        const uint8_t expectedRts[] = {0x80, 0x3c, testCase.rtsValue};
        checkCommand(output.commands[0], expectedDtr, sizeof(expectedDtr));
        checkCommand(output.commands[1], expectedRts, sizeof(expectedRts));
    }
}

void testModemControlRejectsInvalidInputWithoutWriting()
{
    ch9344::CommandSequence output;
    output.count = 0xa5;

    CHECK_EQ(
        ch9344::encodeModemControl(4, true, true, &output),
        ch9344::Error::invalidPort);
    CHECK_EQ(output.count, 0xa5U);
    CHECK_EQ(
        ch9344::encodeModemControl(0, true, true, nullptr),
        ch9344::Error::invalidArgument);
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
    testChipVersionDistinguishesLAndQ();
    testChipVersionRejectsMalformedResponse();
    testUploadModeCommandForSupportedChipVersions();
    testOldLChipDoesNotRequireUploadModeCommand();
    testPortInitializationUsesPerPortRegisterBase();
    testPortInitializationRejectsInvalidInputWithoutWriting();
    testLChipEncodes115200EightNOne();
    testLChipEncodesClockDivisorAndReceiveTimeout();
    testQChipEncodesBaudRateDirectlyAsLittleEndian();
    testUartEncodingRejectsInvalidInputWithoutWriting();
    testModemControlEncodesDtrAndRtsIndependently();
    testModemControlRejectsInvalidInputWithoutWriting();
    return test_support::failures == 0 ? 0 : 1;
}
