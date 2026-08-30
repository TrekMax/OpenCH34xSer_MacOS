#include "../TestSupport.hpp"

#include <ch9344/Protocol.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
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

} // namespace

int main()
{
    testLogicalPortMapping();
    testTxFrameEncoding();
    testTxFrameStopsAtPacketBoundary();
    testTxFrameRejectsInvalidInputWithoutWriting();
    return test_support::failures == 0 ? 0 : 1;
}
