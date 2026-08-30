#include <DriverKit/IOLib.h>
#include <DriverKit/IOUserServer.h>

#include "CH9344Driver.h"

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
    (void)baudRate;
    (void)nDataBits;
    (void)nHalfStopBits;
    (void)parity;
    return kIOReturnUnsupported;
}

kern_return_t IMPL(CH9344Driver, HwProgramBaudRate)
{
    (void)baudRate;
    return kIOReturnUnsupported;
}

kern_return_t IMPL(CH9344Driver, HwProgramMCR)
{
    (void)dtr;
    (void)rts;
    return kIOReturnUnsupported;
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
