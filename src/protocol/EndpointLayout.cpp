#include <ch9344/EndpointLayout.hpp>

ch9344::Error ch9344::classifyEndpoints(
    const EndpointDescriptor* endpoints,
    std::size_t count,
    EndpointLayout* output)
{
    if (endpoints == nullptr || output == nullptr) {
        return Error::invalidArgument;
    }
    if (count != 4) {
        return Error::invalidEndpointLayout;
    }

    uint8_t dataIn = 0;
    uint8_t dataOut = 0;
    uint8_t commandIn = 0;
    uint8_t commandOut = 0;
    uint16_t dataInPacketSize = 0;
    uint16_t dataOutPacketSize = 0;
    uint16_t commandInPacketSize = 0;
    uint16_t commandOutPacketSize = 0;

    for (std::size_t index = 0; index < count; ++index) {
        const EndpointDescriptor& endpoint = endpoints[index];
        if (endpoint.type != EndpointType::bulk || endpoint.maxPacketSize == 0) {
            return Error::invalidEndpointLayout;
        }

        const uint8_t endpointNumber = static_cast<uint8_t>(endpoint.address & 0x0f);
        const bool input = (endpoint.address & 0x80) != 0;
        if (endpointNumber == 2 && input && dataIn == 0) {
            dataIn = endpoint.address;
            dataInPacketSize = endpoint.maxPacketSize;
        } else if (endpointNumber == 2 && !input && dataOut == 0) {
            dataOut = endpoint.address;
            dataOutPacketSize = endpoint.maxPacketSize;
        } else if (endpointNumber == 1 && input && commandIn == 0) {
            commandIn = endpoint.address;
            commandInPacketSize = endpoint.maxPacketSize;
        } else if (endpointNumber == 1 && !input && commandOut == 0) {
            commandOut = endpoint.address;
            commandOutPacketSize = endpoint.maxPacketSize;
        } else {
            return Error::invalidEndpointLayout;
        }
    }

    if (dataIn == 0 || dataOut == 0 || commandIn == 0 || commandOut == 0
        || dataInPacketSize != dataOutPacketSize
        || commandInPacketSize != commandOutPacketSize) {
        return Error::invalidEndpointLayout;
    }

    *output = {
        dataIn,
        dataOut,
        commandIn,
        commandOut,
        dataInPacketSize,
        commandInPacketSize,
    };
    return Error::none;
}
