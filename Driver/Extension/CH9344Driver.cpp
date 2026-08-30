#include <DriverKit/IOLib.h>
#include <DriverKit/IOUserServer.h>

#include "../Shared/DriverCore.hpp"
#include "CH9344Driver.h"

namespace {

constexpr std::uint8_t kPrototypeLogicalPort = 3;

kern_return_t mapConfigurationError(ch9344::driver::DriverCoreError error)
{
    switch (error) {
    case ch9344::driver::DriverCoreError::none:
        return kIOReturnNotReady;
    case ch9344::driver::DriverCoreError::unsupportedLineCoding:
        return kIOReturnUnsupported;
    case ch9344::driver::DriverCoreError::invalidArgument:
    case ch9344::driver::DriverCoreError::protocolError:
        return kIOReturnBadArgument;
    }
}

} // namespace

kern_return_t IMPL(CH9344Driver, Start)
{
    const kern_return_t result = Start(provider, SUPERDISPATCH);
    if (result != kIOReturnSuccess) {
        return result;
    }
    return RegisterService();
}

kern_return_t IMPL(CH9344Driver, Stop)
{
    return Stop(provider, SUPERDISPATCH);
}

kern_return_t IMPL(CH9344Driver, HwResetFIFO)
{
    (void)tx;
    (void)rx;
    return kIOReturnUnsupported;
}

kern_return_t IMPL(CH9344Driver, HwSendBreak)
{
    (void)sendBreak;
    return kIOReturnUnsupported;
}

kern_return_t IMPL(CH9344Driver, HwProgramUART)
{
    ch9344::CommandSequence commands;
    return mapConfigurationError(ch9344::driver::buildUartConfiguration(
        ch9344::ChipVariant::ch9344Q,
        kPrototypeLogicalPort,
        baudRate,
        nDataBits,
        nHalfStopBits,
        parity,
        &commands));
}

kern_return_t IMPL(CH9344Driver, HwProgramBaudRate)
{
    ch9344::CommandSequence commands;
    return mapConfigurationError(ch9344::driver::buildUartConfiguration(
        ch9344::ChipVariant::ch9344Q,
        kPrototypeLogicalPort,
        baudRate,
        8,
        ch9344::driver::kOneStopBitInHalfBits,
        ch9344::driver::kParityNone,
        &commands));
}

kern_return_t IMPL(CH9344Driver, HwProgramMCR)
{
    ch9344::CommandSequence commands;
    return mapConfigurationError(ch9344::driver::buildModemConfiguration(
        kPrototypeLogicalPort,
        dtr,
        rts,
        &commands));
}

kern_return_t IMPL(CH9344Driver, HwGetModemStatus)
{
    (void)cts;
    (void)dsr;
    (void)ri;
    (void)dcd;
    return kIOReturnUnsupported;
}

kern_return_t IMPL(CH9344Driver, HwProgramLatencyTimer)
{
    (void)latency;
    return kIOReturnUnsupported;
}

kern_return_t IMPL(CH9344Driver, HwProgramFlowControl)
{
    (void)arg;
    (void)xon;
    (void)xoff;
    return kIOReturnUnsupported;
}
