#pragma once

#include <cstdint>

namespace ch9344 {

enum class Error {
    none,
    invalidArgument,
    invalidPort,
};

Error mapLogicalPort(uint8_t logicalPort, uint8_t* hardwarePort);

} // namespace ch9344
