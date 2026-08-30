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

ch9344::DecodeResult ch9344::decodeRxTransfer(
    const uint8_t* input,
    std::size_t inputLength,
    RxHandler handler,
    void* context)
{
    if (handler == nullptr) {
        return {Error::invalidArgument, 0, 0};
    }
    if (inputLength == 0) {
        return {Error::none, 0, 0};
    }
    if (input == nullptr) {
        return {Error::invalidArgument, 0, 0};
    }

    std::size_t offset = 0;
    std::size_t recordsDecoded = 0;
    while (offset < inputLength) {
        if (inputLength - offset < 32) {
            return {Error::truncatedRxRecord, recordsDecoded, offset};
        }

        const uint8_t hardwarePort = input[offset];
        if (hardwarePort < 4 || hardwarePort > 7) {
            return {Error::invalidRxPort, recordsDecoded, offset};
        }

        const uint8_t payloadLength = input[offset + 1];
        if (payloadLength > 30) {
            return {Error::invalidRxLength, recordsDecoded, offset};
        }

        const RxRecordView record {
            static_cast<uint8_t>(hardwarePort - 4),
            input + offset + 2,
            payloadLength,
        };
        handler(context, record);

        ++recordsDecoded;
        offset += 32;
    }

    return {Error::none, recordsDecoded, offset};
}

ch9344::Error ch9344::parseChipVersion(
    const uint8_t* response,
    std::size_t length,
    ChipInfo* info)
{
    if (response == nullptr || info == nullptr) {
        return Error::invalidArgument;
    }
    if (length != 4) {
        return Error::invalidVersionResponse;
    }

    info->variant = response[0] >= 0x40
        ? ChipVariant::ch9344Q
        : ChipVariant::ch9344L;
    info->version = response[0];
    return Error::none;
}

ch9344::Error ch9344::encodeDeviceInitialization(
    const ChipInfo& chip,
    CommandSequence* output)
{
    if (output == nullptr) {
        return Error::invalidArgument;
    }

    *output = {};
    const bool uploadModeRequired = chip.variant == ChipVariant::ch9344Q
        || (chip.variant == ChipVariant::ch9344L && chip.version >= 0x39);
    if (!uploadModeRequired) {
        return Error::none;
    }

    const uint8_t command[] = {0x94, 0x9d, 0x01, 0, 0, 0, 0, 0};
    std::memcpy(output->commands[0].bytes, command, sizeof(command));
    output->commands[0].length = sizeof(command);
    output->count = 1;
    return Error::none;
}

ch9344::Error ch9344::encodePortInitialization(
    uint8_t logicalPort,
    CommandSequence* output)
{
    if (output == nullptr) {
        return Error::invalidArgument;
    }

    uint8_t hardwarePort = 0;
    const Error portError = mapLogicalPort(logicalPort, &hardwarePort);
    if (portError != Error::none) {
        return portError;
    }

    *output = {};
    const uint8_t registerBase = static_cast<uint8_t>(
        0x10 * (hardwarePort - 4) + 0x08);
    const uint8_t commands[][3] = {
        {0xc0, static_cast<uint8_t>(registerBase + 0x02), 0x87},
        {0xc0, static_cast<uint8_t>(registerBase + 0x03), 0x03},
        {0xc0, static_cast<uint8_t>(registerBase + 0x04), 0x08},
    };

    for (std::size_t index = 0; index < 3; ++index) {
        std::memcpy(output->commands[index].bytes, commands[index], 3);
        output->commands[index].length = 3;
    }
    output->count = 3;
    return Error::none;
}
