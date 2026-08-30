#include <ch9344/Protocol.hpp>

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
