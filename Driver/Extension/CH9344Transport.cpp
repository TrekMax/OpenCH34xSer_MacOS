#include <DriverKit/IOLib.h>
#include <USBDriverKit/AppleUSBDefinitions.h>
#include <USBDriverKit/USBDriverKitDefs.h>

#include "../Shared/DriverCore.hpp"
#include "../Shared/PortScheduler.hpp"
#include "../Shared/UsbTransaction.hpp"
#include "CH9344Driver.h"
#include "CH9344Transport.h"
#include "DriverKitUsbBackend.hpp"

namespace {

constexpr std::uint32_t kDataBufferCapacity = 512;
constexpr std::uint32_t kDataTransferTimeoutMilliseconds = 5000;
constexpr const char* kSerialPortPropertyKeys[4] = {
    "CH9344SerialPort1",
    "CH9344SerialPort2",
    "CH9344SerialPort3",
    "CH9344SerialPort4",
};

kern_return_t mapConfigurationError(ch9344::driver::DriverCoreError error)
{
    switch (error) {
    case ch9344::driver::DriverCoreError::none:
        return kIOReturnSuccess;
    case ch9344::driver::DriverCoreError::unsupportedLineCoding:
        return kIOReturnUnsupported;
    case ch9344::driver::DriverCoreError::invalidArgument:
    case ch9344::driver::DriverCoreError::invalidRing:
    case ch9344::driver::DriverCoreError::rxBackpressure:
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

struct CH9344Transport_IVars {
    DriverKitUsbState usb;
    ch9344::ChipInfo chip;
    CH9344Driver* ports[4];
    IOLock* lock;
    IOBufferMemoryDescriptor* dataInBuffer;
    IOBufferMemoryDescriptor* dataOutBuffer;
    IOAddressSegment dataInRange;
    IOAddressSegment dataOutRange;
    OSAction* dataInAction;
    OSAction* dataOutAction;
    std::uint32_t rxTransferLength;
    std::uint32_t txFrameLength;
    std::uint32_t txNextConsumer;
    std::uint8_t txPort;
    std::uint8_t txCursor;
    std::uint8_t rxPendingMask;
    bool transportReady;
    bool active;
    bool dataInPending;
    bool dataOutPending;
    bool rxTransferWaiting;
};

bool CH9344Transport::init()
{
    if (!super::init()) {
        return false;
    }
    ivars = IONewZero(CH9344Transport_IVars, 1);
    if (ivars == nullptr) {
        return false;
    }
    ivars->lock = IOLockAlloc();
    if (ivars->lock == nullptr) {
        IOSafeDeleteNULL(ivars, CH9344Transport_IVars, 1);
        return false;
    }
    return true;
}

kern_return_t IMPL(CH9344Transport, Start)
{
    kern_return_t result = Start(provider, SUPERDISPATCH);
    if (result != kIOReturnSuccess) {
        return result;
    }

    ivars->usb.owner = this;
    ivars->usb.interface = OSDynamicCast(IOUSBHostInterface, provider);
    if (ivars->usb.interface == nullptr) {
        Stop(provider, SUPERDISPATCH);
        return kIOReturnBadArgument;
    }

    DriverKitUsbBackend backend(&ivars->usb);
    const auto startup = ch9344::driver::initializeAllPortsUsbTransport(
        backend, 115200);
    result = mapStartupError(startup.error);
    if (result != kIOReturnSuccess) {
        return result;
    }
    ivars->chip = startup.chip;

    result = CreateDataResources();
    if (result != kIOReturnSuccess) {
        ch9344::driver::shutdownUsbTransport(backend);
        return result;
    }
    ivars->transportReady = true;
    ivars->active = true;

    result = CreateSerialPorts();
    if (result == kIOReturnSuccess) {
        result = RegisterService();
    }
    if (result != kIOReturnSuccess) {
        TerminateSerialPorts();
        DeactivateTransport();
        return result;
    }

    IOLockLock(ivars->lock);
    SubmitDataInLocked();
    IOLockUnlock(ivars->lock);
    return kIOReturnSuccess;
}

kern_return_t IMPL(CH9344Transport, Stop)
{
    if (ivars != nullptr) {
        IOLockLock(ivars->lock);
        ivars->active = false;
        IOLockUnlock(ivars->lock);
        TerminateSerialPorts();
        DeactivateTransport();
    }
    return Stop(provider, SUPERDISPATCH);
}

void CH9344Transport::free()
{
    if (ivars != nullptr) {
        TerminateSerialPorts();
        DeactivateTransport();
        OSSafeReleaseNULL(ivars->usb.dataOut);
        OSSafeReleaseNULL(ivars->usb.commandOut);
        OSSafeReleaseNULL(ivars->usb.dataIn);
        OSSafeReleaseNULL(ivars->usb.commandIn);
        OSSafeReleaseNULL(ivars->usb.commandBuffer);
        IOLockFreeZero(ivars->lock);
        IOSafeDeleteNULL(ivars, CH9344Transport_IVars, 1);
    }
    super::free();
}

kern_return_t CH9344Transport::CreateSerialPorts()
{
    for (std::uint8_t logicalPort = 0;
         logicalPort < ch9344::driver::kPortCount;
         ++logicalPort) {
        IOService* service = nullptr;
        kern_return_t result = Create(
            this,
            kSerialPortPropertyKeys[logicalPort],
            &service);
        if (result != kIOReturnSuccess || service == nullptr) {
            OSSafeReleaseNULL(service);
            TerminateSerialPorts();
            return result == kIOReturnSuccess ? kIOReturnNoMemory : result;
        }
        CH9344Driver* port = OSDynamicCast(CH9344Driver, service);
        if (port == nullptr || !port->ConfigurePort(logicalPort)) {
            service->Terminate(0);
            service->release();
            TerminateSerialPorts();
            return kIOReturnBadArgument;
        }
        ivars->ports[logicalPort] = port;
        result = port->RegisterService();
        if (result != kIOReturnSuccess) {
            TerminateSerialPorts();
            return result;
        }
    }
    return kIOReturnSuccess;
}

void CH9344Transport::TerminateSerialPorts()
{
    if (ivars == nullptr) {
        return;
    }
    for (std::size_t index = ch9344::driver::kPortCount; index > 0; --index) {
        CH9344Driver*& port = ivars->ports[index - 1];
        if (port != nullptr) {
            port->Terminate(0);
            OSSafeReleaseNULL(port);
        }
    }
}

kern_return_t CH9344Transport::CreateDataResources()
{
    kern_return_t result = ivars->usb.interface->CreateIOBuffer(
        kIOMemoryDirectionIn,
        kDataBufferCapacity,
        &ivars->dataInBuffer);
    if (result == kIOReturnSuccess) {
        result = ivars->usb.interface->CreateIOBuffer(
            kIOMemoryDirectionOut,
            kDataBufferCapacity,
            &ivars->dataOutBuffer);
    }
    if (result == kIOReturnSuccess) {
        result = ivars->dataInBuffer->GetAddressRange(&ivars->dataInRange);
    }
    if (result == kIOReturnSuccess) {
        result = ivars->dataOutBuffer->GetAddressRange(&ivars->dataOutRange);
    }
    if (result == kIOReturnSuccess &&
        (ivars->dataInRange.address == 0 ||
         ivars->dataInRange.length < kDataBufferCapacity ||
         ivars->dataOutRange.address == 0 ||
         ivars->dataOutRange.length < kDataBufferCapacity)) {
        result = kIOReturnNoMemory;
    }
    if (result == kIOReturnSuccess) {
        result = CreateActionDataInComplete(0, &ivars->dataInAction);
    }
    if (result == kIOReturnSuccess) {
        result = CreateActionDataOutComplete(0, &ivars->dataOutAction);
    }
    if (result != kIOReturnSuccess) {
        ReleaseDataResources();
    }
    return result;
}

void CH9344Transport::ReleaseDataResources()
{
    if (ivars == nullptr) {
        return;
    }
    OSSafeReleaseNULL(ivars->dataOutAction);
    OSSafeReleaseNULL(ivars->dataInAction);
    OSSafeReleaseNULL(ivars->dataOutBuffer);
    OSSafeReleaseNULL(ivars->dataInBuffer);
    ivars->dataInRange = {};
    ivars->dataOutRange = {};
}

bool CH9344Transport::IsTransportReady()
{
    if (ivars == nullptr) {
        return false;
    }
    IOLockLock(ivars->lock);
    const bool ready = ivars->active && ivars->transportReady;
    IOLockUnlock(ivars->lock);
    return ready;
}

void CH9344Transport::NotifyTx(std::uint8_t logicalPort)
{
    if (ivars == nullptr || logicalPort >= ch9344::driver::kPortCount) {
        return;
    }
    IOLockLock(ivars->lock);
    StartTxLocked();
    IOLockUnlock(ivars->lock);
}

void CH9344Transport::NotifyRxSpace(std::uint8_t logicalPort)
{
    if (ivars == nullptr || logicalPort >= ch9344::driver::kPortCount) {
        return;
    }
    IOLockLock(ivars->lock);
    if (ivars->rxTransferWaiting &&
        (ivars->rxPendingMask & (1U << logicalPort)) != 0) {
        DeliverPendingRxLocked();
    } else if (!ivars->rxTransferWaiting) {
        SubmitDataInLocked();
    }
    IOLockUnlock(ivars->lock);
}

kern_return_t CH9344Transport::ProgramUART(
    std::uint8_t logicalPort,
    std::uint32_t baudRate,
    std::uint8_t dataBits,
    std::uint8_t halfStopBits,
    std::uint8_t parity)
{
    if (ivars == nullptr) {
        return kIOReturnNotReady;
    }
    IOLockLock(ivars->lock);
    if (!ivars->active || !ivars->transportReady) {
        IOLockUnlock(ivars->lock);
        return kIOReturnNotReady;
    }
    ch9344::CommandSequence commands;
    const kern_return_t configurationResult = mapConfigurationError(
        ch9344::driver::buildUartConfiguration(
            ivars->chip.variant,
            logicalPort,
            baudRate,
            dataBits,
            halfStopBits,
            parity,
            &commands));
    if (configurationResult != kIOReturnSuccess) {
        IOLockUnlock(ivars->lock);
        return configurationResult;
    }
    DriverKitUsbBackend backend(&ivars->usb);
    const bool submitted = ch9344::driver::submitCommandSequence(
        backend, commands);
    IOLockUnlock(ivars->lock);
    return submitted ? kIOReturnSuccess : kIOReturnIOError;
}

kern_return_t CH9344Transport::ProgramModemControl(
    std::uint8_t logicalPort,
    bool dtr,
    bool rts)
{
    if (ivars == nullptr) {
        return kIOReturnNotReady;
    }
    IOLockLock(ivars->lock);
    if (!ivars->active || !ivars->transportReady) {
        IOLockUnlock(ivars->lock);
        return kIOReturnNotReady;
    }
    ch9344::CommandSequence commands;
    const kern_return_t configurationResult = mapConfigurationError(
        ch9344::driver::buildModemConfiguration(
            logicalPort, dtr, rts, &commands));
    if (configurationResult != kIOReturnSuccess) {
        IOLockUnlock(ivars->lock);
        return configurationResult;
    }
    DriverKitUsbBackend backend(&ivars->usb);
    const bool submitted = ch9344::driver::submitCommandSequence(
        backend, commands);
    IOLockUnlock(ivars->lock);
    return submitted ? kIOReturnSuccess : kIOReturnIOError;
}

void CH9344Transport::StartTxLocked()
{
    if (!ivars->active || !ivars->transportReady || ivars->dataOutPending ||
        ivars->usb.dataOut == nullptr || ivars->dataOutBuffer == nullptr ||
        ivars->dataOutAction == nullptr) {
        return;
    }

    std::uint8_t readyMask = 0;
    for (std::uint8_t port = 0; port < ch9344::driver::kPortCount; ++port) {
        if (ivars->ports[port] != nullptr && ivars->ports[port]->HasPendingTx()) {
            readyMask |= static_cast<std::uint8_t>(1U << port);
        }
    }
    const auto selection = ch9344::driver::selectNextTxPort(
        readyMask, ivars->txCursor);
    if (!selection.found) {
        return;
    }

    std::uint32_t frameLength = 0;
    std::uint32_t nextConsumer = 0;
    CH9344Driver* port = ivars->ports[selection.logicalPort];
    if (port == nullptr || !port->PrepareTx(
            reinterpret_cast<std::uint8_t*>(ivars->dataOutRange.address),
            kDataBufferCapacity,
            &frameLength,
            &nextConsumer)) {
        return;
    }
    if (ivars->dataOutBuffer->SetLength(frameLength) != kIOReturnSuccess) {
        return;
    }

    ivars->txPort = selection.logicalPort;
    ivars->txCursor = selection.nextCursor;
    ivars->txFrameLength = frameLength;
    ivars->txNextConsumer = nextConsumer;
    ivars->dataOutPending = true;
    const kern_return_t result = ivars->usb.dataOut->AsyncIO(
        ivars->dataOutBuffer,
        frameLength,
        ivars->dataOutAction,
        kDataTransferTimeoutMilliseconds);
    if (result != kIOReturnSuccess) {
        ivars->dataOutPending = false;
    }
}

void CH9344Transport::SubmitDataInLocked()
{
    if (!ivars->active || !ivars->transportReady || ivars->rxTransferWaiting ||
        ivars->dataInPending || ivars->dataInBuffer == nullptr ||
        ivars->usb.dataIn == nullptr || ivars->dataInAction == nullptr) {
        return;
    }
    if (ivars->dataInBuffer->SetLength(kDataBufferCapacity) != kIOReturnSuccess) {
        return;
    }
    ivars->dataInPending = true;
    const kern_return_t result = ivars->usb.dataIn->AsyncIO(
        ivars->dataInBuffer,
        kDataBufferCapacity,
        ivars->dataInAction,
        0);
    if (result != kIOReturnSuccess) {
        ivars->dataInPending = false;
    }
}

void CH9344Transport::DeliverPendingRxLocked()
{
    if (!ivars->rxTransferWaiting) {
        return;
    }
    const auto backpressure = static_cast<std::int32_t>(
        ch9344::driver::DriverCoreError::rxBackpressure);
    while (ivars->rxPendingMask != 0) {
        std::uint8_t logicalPort = 0;
        while ((ivars->rxPendingMask & (1U << logicalPort)) == 0) {
            ++logicalPort;
        }
        CH9344Driver* port = ivars->ports[logicalPort];
        if (port == nullptr) {
            ivars->rxPendingMask = ch9344::driver::markRxPortDelivered(
                ivars->rxPendingMask, logicalPort);
            continue;
        }
        const std::int32_t result = port->DeliverRx(
            reinterpret_cast<const std::uint8_t*>(ivars->dataInRange.address),
            ivars->rxTransferLength);
        if (result == backpressure) {
            return;
        }
        if (result != static_cast<std::int32_t>(
                ch9344::driver::DriverCoreError::none)) {
            port->ReportRxProtocolError();
        }
        ivars->rxPendingMask = ch9344::driver::markRxPortDelivered(
            ivars->rxPendingMask, logicalPort);
    }
    ivars->rxTransferWaiting = false;
    SubmitDataInLocked();
}

void IMPL(CH9344Transport, DataInComplete)
{
    (void)action;
    (void)completionTimestamp;
    if (ivars == nullptr) {
        return;
    }
    IOLockLock(ivars->lock);
    ivars->dataInPending = false;
    if (!ivars->active) {
        IOLockUnlock(ivars->lock);
        return;
    }
    if (status != kIOReturnSuccess || actualByteCount > kDataBufferCapacity) {
        IOLockUnlock(ivars->lock);
        return;
    }

    const auto inspection = ch9344::driver::inspectRxPorts(
        reinterpret_cast<const std::uint8_t*>(ivars->dataInRange.address),
        actualByteCount);
    if (inspection.error != ch9344::Error::none) {
        for (CH9344Driver* port : ivars->ports) {
            if (port != nullptr) {
                port->ReportRxProtocolError();
            }
        }
        SubmitDataInLocked();
        IOLockUnlock(ivars->lock);
        return;
    }
    ivars->rxTransferLength = actualByteCount;
    ivars->rxPendingMask = inspection.portMask;
    ivars->rxTransferWaiting = true;
    DeliverPendingRxLocked();
    IOLockUnlock(ivars->lock);
}

void IMPL(CH9344Transport, DataOutComplete)
{
    (void)action;
    (void)completionTimestamp;
    if (ivars == nullptr) {
        return;
    }
    IOLockLock(ivars->lock);
    ivars->dataOutPending = false;
    if (!ivars->active) {
        IOLockUnlock(ivars->lock);
        return;
    }
    const bool succeeded = status == kIOReturnSuccess &&
        actualByteCount == ivars->txFrameLength;
    CH9344Driver* port = ivars->ports[ivars->txPort];
    if (port != nullptr) {
        port->CompleteTx(
            succeeded,
            ivars->txFrameLength,
            actualByteCount,
            ivars->txNextConsumer);
    }
    if (succeeded) {
        StartTxLocked();
    }
    IOLockUnlock(ivars->lock);
}

void CH9344Transport::DeactivateTransport()
{
    if (ivars == nullptr) {
        return;
    }
    IOLockLock(ivars->lock);
    ivars->active = false;
    const bool abortIn = ivars->dataInPending;
    const bool abortOut = ivars->dataOutPending;
    IOLockUnlock(ivars->lock);

    if (ivars->usb.dataIn != nullptr && abortIn) {
        ivars->usb.dataIn->Abort(
            kIOUSBAbortSynchronous,
            kIOReturnAborted,
            nullptr);
    }
    if (ivars->usb.dataOut != nullptr && abortOut) {
        ivars->usb.dataOut->Abort(
            kIOUSBAbortSynchronous,
            kIOReturnAborted,
            nullptr);
    }

    IOLockLock(ivars->lock);
    ivars->dataInPending = false;
    ivars->dataOutPending = false;
    ivars->rxTransferWaiting = false;
    ivars->rxPendingMask = 0;
    ivars->transportReady = false;
    IOLockUnlock(ivars->lock);
    ReleaseDataResources();

    if (ivars->usb.opened) {
        DriverKitUsbBackend backend(&ivars->usb);
        ch9344::driver::shutdownUsbTransport(backend);
    }
}
