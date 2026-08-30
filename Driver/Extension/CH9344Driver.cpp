#include <DriverKit/IOLib.h>
#include <DriverKit/IOUserServer.h>

#include "../Shared/DriverCore.hpp"
#include "../Shared/UsbTransaction.hpp"
#include "CH9344Driver.h"
#include "DriverKitUsbBackend.hpp"

namespace {

constexpr std::uint8_t kPrototypeLogicalPort = 3;

kern_return_t mapConfigurationError(ch9344::driver::DriverCoreError error)
{
    switch (error) {
    case ch9344::driver::DriverCoreError::none:
        return kIOReturnSuccess;
    case ch9344::driver::DriverCoreError::unsupportedLineCoding:
        return kIOReturnUnsupported;
    case ch9344::driver::DriverCoreError::invalidArgument:
    case ch9344::driver::DriverCoreError::protocolError:
        return kIOReturnBadArgument;
    }
    return kIOReturnBadArgument;
}

kern_return_t mapStartupError(ch9344::driver::UsbStartupError error)
{
    switch (error) {
    case ch9344::driver::UsbStartupError::none:
        return kIOReturnSuccess;
    case ch9344::driver::UsbStartupError::openFailed:
        return kIOReturnNotOpen;
    case ch9344::driver::UsbStartupError::pipeFailed:
        return kIOReturnNotFound;
    case ch9344::driver::UsbStartupError::invalidVersion:
    case ch9344::driver::UsbStartupError::protocolError:
        return kIOReturnUnsupported;
    case ch9344::driver::UsbStartupError::versionTransferFailed:
    case ch9344::driver::UsbStartupError::commandTransferFailed:
        return kIOReturnIOError;
    }
    return kIOReturnError;
}

} // namespace

struct CH9344Driver_IVars {
    DriverKitUsbState usb;
    ch9344::ChipInfo chip;
    bool transportReady;
};

bool CH9344Driver::init()
{
    if (!super::init()) {
        return false;
    }
    ivars = IONewZero(CH9344Driver_IVars, 1);
    return ivars != nullptr;
}

kern_return_t IMPL(CH9344Driver, Start)
{
    kern_return_t result = Start(provider, SUPERDISPATCH);
    if (result != kIOReturnSuccess) {
        return result;
    }

    ivars->usb.owner = this;
    ivars->usb.interface = OSDynamicCast(IOUSBHostInterface, provider);
    if (ivars->usb.interface == nullptr) {
        return kIOReturnBadArgument;
    }

    DriverKitUsbBackend backend(&ivars->usb);
    const auto startup = ch9344::driver::initializeUsbTransport(
        backend,
        kPrototypeLogicalPort,
        115200);
    result = mapStartupError(startup.error);
    if (result != kIOReturnSuccess) {
        return result;
    }

    ivars->chip = startup.chip;
    ivars->transportReady = true;
    result = RegisterService();
    if (result != kIOReturnSuccess) {
        ch9344::driver::shutdownUsbTransport(backend);
        ivars->transportReady = false;
    }
    return result;
}

kern_return_t IMPL(CH9344Driver, Stop)
{
    if (ivars != nullptr && ivars->usb.opened) {
        DriverKitUsbBackend backend(&ivars->usb);
        ch9344::driver::shutdownUsbTransport(backend);
        ivars->transportReady = false;
    }
    return Stop(provider, SUPERDISPATCH);
}

void CH9344Driver::free()
{
    if (ivars != nullptr) {
        OSSafeReleaseNULL(ivars->usb.dataOut);
        OSSafeReleaseNULL(ivars->usb.commandOut);
        OSSafeReleaseNULL(ivars->usb.dataIn);
        OSSafeReleaseNULL(ivars->usb.commandIn);
        OSSafeReleaseNULL(ivars->usb.commandBuffer);
        IOSafeDeleteNULL(ivars, CH9344Driver_IVars, 1);
    }
    super::free();
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
    const kern_return_t result = mapConfigurationError(
        ch9344::driver::buildUartConfiguration(
        ch9344::ChipVariant::ch9344Q,
        kPrototypeLogicalPort,
        baudRate,
        nDataBits,
        nHalfStopBits,
        parity,
        &commands));
    if (result != kIOReturnSuccess) {
        return result;
    }
    if (ivars == nullptr || !ivars->transportReady) {
        return kIOReturnNotReady;
    }
    DriverKitUsbBackend backend(&ivars->usb);
    return ch9344::driver::submitCommandSequence(backend, commands)
        ? kIOReturnSuccess
        : kIOReturnIOError;
}

kern_return_t IMPL(CH9344Driver, HwProgramBaudRate)
{
    ch9344::CommandSequence commands;
    const kern_return_t result = mapConfigurationError(
        ch9344::driver::buildUartConfiguration(
        ivars != nullptr ? ivars->chip.variant : ch9344::ChipVariant::ch9344Q,
        kPrototypeLogicalPort,
        baudRate,
        8,
        ch9344::driver::kOneStopBitInHalfBits,
        ch9344::driver::kParityNone,
        &commands));
    if (result != kIOReturnSuccess) {
        return result;
    }
    if (ivars == nullptr || !ivars->transportReady) {
        return kIOReturnNotReady;
    }
    DriverKitUsbBackend backend(&ivars->usb);
    return ch9344::driver::submitCommandSequence(backend, commands)
        ? kIOReturnSuccess
        : kIOReturnIOError;
}

kern_return_t IMPL(CH9344Driver, HwProgramMCR)
{
    ch9344::CommandSequence commands;
    const kern_return_t result = mapConfigurationError(
        ch9344::driver::buildModemConfiguration(
        kPrototypeLogicalPort,
        dtr,
        rts,
        &commands));
    if (result != kIOReturnSuccess) {
        return result;
    }
    if (ivars == nullptr || !ivars->transportReady) {
        return kIOReturnNotReady;
    }
    DriverKitUsbBackend backend(&ivars->usb);
    return ch9344::driver::submitCommandSequence(backend, commands)
        ? kIOReturnSuccess
        : kIOReturnIOError;
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
