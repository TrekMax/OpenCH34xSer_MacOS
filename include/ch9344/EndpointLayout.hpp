#pragma once

#include <ch9344/Protocol.hpp>

#include <cstddef>
#include <cstdint>

namespace ch9344 {

enum class EndpointType {
    bulk,
    other,
};

struct EndpointDescriptor {
    uint8_t address;
    EndpointType type;
    uint16_t maxPacketSize;
};

struct EndpointLayout {
    uint8_t dataIn;
    uint8_t dataOut;
    uint8_t commandIn;
    uint8_t commandOut;
    uint16_t dataMaxPacketSize;
    uint16_t commandMaxPacketSize;
};

Error classifyEndpoints(
    const EndpointDescriptor* endpoints,
    std::size_t count,
    EndpointLayout* output);

} // namespace ch9344
