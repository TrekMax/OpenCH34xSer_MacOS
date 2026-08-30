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
    if (framePayload != 0 && payload != nullptr) {
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

ch9344::Error ch9344::encodeUart8N1(
    ChipVariant variant,
    uint8_t logicalPort,
    uint32_t baudRate,
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
    if (baudRate == 0 || baudRate > 12000000) {
        return Error::invalidBaudRate;
    }

    *output = {};
    const uint8_t registerBase = static_cast<uint8_t>(
        0x10 * (hardwarePort - 4) + 0x08);
    const uint32_t clockRate = baudRate > 115200 ? 44236800 : 1843200;
    uint32_t divisor = 0;
    if (baudRate == 2000000) {
        divisor = 2;
    } else {
        uint32_t decimalDivisor = 10 * clockRate / 16 / baudRate;
        const uint32_t tenths = decimalDivisor % 10;
        divisor = decimalDivisor / 10;
        if (tenths >= 5) {
            ++divisor;
        }
    }

    uint8_t baudSelector = 0;
    switch (baudRate) {
    case 250000:
        baudSelector = 1;
        break;
    case 500000:
        baudSelector = 2;
        break;
    case 1000000:
        baudSelector = 3;
        break;
    case 1500000:
        baudSelector = 4;
        break;
    case 3000000:
        baudSelector = 5;
        break;
    case 12000000:
        baudSelector = 6;
        break;
    default:
        break;
    }

    const uint8_t receiveTimeout = baudRate >= 921600
        ? 5
        : static_cast<uint8_t>((15000000 / baudRate) / 100 + 1);

    const uint8_t clockCommand[] = {
        0x80,
        static_cast<uint8_t>(registerBase + 0x01),
        static_cast<uint8_t>(baudRate > 115200 ? 0x51 : 0x50),
    };
    const uint8_t lBaudCommand[] = {
        0x20,
        static_cast<uint8_t>(registerBase + 0x03),
        static_cast<uint8_t>(divisor & 0xff),
        static_cast<uint8_t>((divisor >> 8) & 0xff),
        baudSelector,
        0x00,
    };
    const uint8_t qBaudCommand[] = {
        0x20,
        static_cast<uint8_t>(registerBase + 0x03),
        0x00,
        0x00,
        0x00,
        static_cast<uint8_t>(baudRate & 0xff),
        static_cast<uint8_t>((baudRate >> 8) & 0xff),
        static_cast<uint8_t>((baudRate >> 16) & 0xff),
        static_cast<uint8_t>((baudRate >> 24) & 0xff),
    };
    const uint8_t formatCommand[] = {
        0xc0,
        static_cast<uint8_t>(registerBase + 0x03),
        0x03,
    };
    const uint8_t timeoutCommand[] = {
        static_cast<uint8_t>(0x90 + hardwarePort),
        0x9c,
        hardwarePort,
        receiveTimeout,
    };
    const uint8_t controlCommand[] = {
        0xc0,
        static_cast<uint8_t>(registerBase + 0x01),
        0x0f,
    };
    const uint8_t readCommand[] = {
        0x90,
        0x85,
        static_cast<uint8_t>(registerBase | 0x06),
    };

    const auto append = [&](const uint8_t* bytes, std::size_t length) {
        Command& command = output->commands[output->count++];
        std::memcpy(command.bytes, bytes, length);
        command.length = length;
    };
    append(clockCommand, sizeof(clockCommand));
    if (variant == ChipVariant::ch9344L) {
        append(lBaudCommand, sizeof(lBaudCommand));
    } else {
        append(qBaudCommand, sizeof(qBaudCommand));
    }
    append(formatCommand, sizeof(formatCommand));
    append(timeoutCommand, sizeof(timeoutCommand));
    append(controlCommand, sizeof(controlCommand));
    append(readCommand, sizeof(readCommand));
    return Error::none;
}

ch9344::Error ch9344::encodeModemControl(
    uint8_t logicalPort,
    bool dtr,
    bool rts,
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
    const uint8_t controlRegister = static_cast<uint8_t>(
        0x10 * (hardwarePort - 4) + 0x0c);
    const uint8_t commands[][3] = {
        {0x80, controlRegister, static_cast<uint8_t>(dtr ? 0x01 : 0x00)},
        {0x80, controlRegister, static_cast<uint8_t>(rts ? 0x11 : 0x10)},
    };

    for (std::size_t index = 0; index < 2; ++index) {
        std::memcpy(output->commands[index].bytes, commands[index], 3);
        output->commands[index].length = 3;
    }
    output->count = 2;
    return Error::none;
}
