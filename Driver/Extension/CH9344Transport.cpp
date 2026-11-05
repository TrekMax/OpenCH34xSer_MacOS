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
    IOBufferMemoryDescriptor* statusInBuffer;
    IOAddressSegment dataInRange;
    IOAddressSegment dataOutRange;
    IOAddressSegment statusInRange;
    OSAction* dataInAction;
    OSAction* dataOutAction;
    OSAction* statusInAction;
    ch9344::driver::TxFlowControl txFlow;
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
    bool statusInPending;
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
    ivars->txFlow.reset();
    ivars->usb.asyncStatusReader = true;

    IOLockLock(ivars->lock);
    result = SubmitStatusInLocked();
    IOLockUnlock(ivars->lock);
    if (result != kIOReturnSuccess) {
        DeactivateTransport();
        return result;
    }

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
    os_log(OS_LOG_DEFAULT, "CH9344Transport Stop begin");
    if (ivars != nullptr) {
        IOLockLock(ivars->lock);
        ivars->active = false;
        IOLockUnlock(ivars->lock);
        TerminateSerialPorts();
        os_log(OS_LOG_DEFAULT, "CH9344Transport Stop children terminated");
        DeactivateTransport();
        os_log(OS_LOG_DEFAULT, "CH9344Transport Stop USB deactivated");
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
            os_log(OS_LOG_DEFAULT, "CH9344Transport Terminate port %zu begin", index - 1);
            const kern_return_t result = port->Terminate(0);
            os_log(OS_LOG_DEFAULT, "CH9344Transport Terminate port %zu returned 0x%x", index - 1, result);
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
            // The driver writes the frame before the USB controller reads it.
            // Out-only memory is mapped read-only in the driver process.
            kIOMemoryDirectionOutIn,
            kDataBufferCapacity,
            &ivars->dataOutBuffer);
    }
    if (result == kIOReturnSuccess) {
        result = ivars->usb.interface->CreateIOBuffer(
            kIOMemoryDirectionIn,
            kDataBufferCapacity,
            &ivars->statusInBuffer);
    }
    if (result == kIOReturnSuccess) {
        result = ivars->dataInBuffer->GetAddressRange(&ivars->dataInRange);
    }
    if (result == kIOReturnSuccess) {
        result = ivars->dataOutBuffer->GetAddressRange(&ivars->dataOutRange);
    }
    if (result == kIOReturnSuccess) {
        result = ivars->statusInBuffer->GetAddressRange(&ivars->statusInRange);
    }
    if (result == kIOReturnSuccess &&
        (ivars->dataInRange.address == 0 ||
         ivars->dataInRange.length < kDataBufferCapacity ||
         ivars->dataOutRange.address == 0 ||
         ivars->dataOutRange.length < kDataBufferCapacity ||
         ivars->statusInRange.address == 0 ||
         ivars->statusInRange.length < kDataBufferCapacity)) {
        result = kIOReturnNoMemory;
    }
    if (result == kIOReturnSuccess) {
        result = CreateActionDataInComplete(0, &ivars->dataInAction);
    }
    if (result == kIOReturnSuccess) {
        result = CreateActionDataOutComplete(0, &ivars->dataOutAction);
    }
    if (result == kIOReturnSuccess) {
        result = CreateActionStatusInComplete(0, &ivars->statusInAction);
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
    OSSafeReleaseNULL(ivars->statusInAction);
    OSSafeReleaseNULL(ivars->dataInAction);
    OSSafeReleaseNULL(ivars->dataOutBuffer);
    OSSafeReleaseNULL(ivars->statusInBuffer);
    OSSafeReleaseNULL(ivars->dataInBuffer);
    ivars->dataInRange = {};
    ivars->dataOutRange = {};
    ivars->statusInRange = {};
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
    const auto selection = ivars->txFlow.select(
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
    ivars->txFlow.submitted(selection.logicalPort);
    const kern_return_t result = ivars->usb.dataOut->AsyncIO(
        ivars->dataOutBuffer,
        frameLength,
        ivars->dataOutAction,
        kDataTransferTimeoutMilliseconds);
    if (result != kIOReturnSuccess) {
        ivars->dataOutPending = false;
        ivars->txFlow.ready(static_cast<std::uint8_t>(1U << selection.logicalPort));
        os_log(OS_LOG_DEFAULT, "CH9344Transport TX submission failed 0x%x", result);
    }
}

kern_return_t CH9344Transport::SubmitStatusInLocked()
{
    if (!ivars->active || !ivars->transportReady || ivars->statusInPending ||
        ivars->statusInBuffer == nullptr || ivars->usb.commandIn == nullptr ||
        ivars->statusInAction == nullptr) {
        return kIOReturnNotReady;
    }
    kern_return_t result = ivars->statusInBuffer->SetLength(kDataBufferCapacity);
    if (result != kIOReturnSuccess) {
        return result;
    }
    ivars->statusInPending = true;
    result = ivars->usb.commandIn->AsyncIO(
        ivars->statusInBuffer, kDataBufferCapacity, ivars->statusInAction, 0);
    if (result != kIOReturnSuccess) {
        ivars->statusInPending = false;
        os_log(OS_LOG_DEFAULT, "CH9344Transport status submission failed 0x%x", result);
    }
    return result;
}

void IMPL(CH9344Transport, StatusInComplete)
{
    (void)action;
    (void)completionTimestamp;
    if (ivars == nullptr) {
        return;
    }
    IOLockLock(ivars->lock);
    ivars->statusInPending = false;
    if (!ivars->active) {
        IOLockUnlock(ivars->lock);
        return;
    }
    if (status != kIOReturnSuccess || actualByteCount > kDataBufferCapacity) {
        os_log(OS_LOG_DEFAULT, "CH9344Transport status completion failed 0x%x bytes %u", status, actualByteCount);
        ivars->transportReady = false;
        IOLockUnlock(ivars->lock);
        return;
    }
    const auto inspection = ch9344::driver::inspectCommandStatus(
        reinterpret_cast<const std::uint8_t*>(ivars->statusInRange.address),
        actualByteCount);
    if (inspection.valid) {
        ivars->txFlow.ready(inspection.txReadyMask);
    } else {
        os_log(OS_LOG_DEFAULT, "CH9344Transport invalid status record bytes %u", actualByteCount);
    }
    if (SubmitStatusInLocked() == kIOReturnSuccess) {
        StartTxLocked();
    } else {
        ivars->transportReady = false;
    }
    IOLockUnlock(ivars->lock);
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
    } else {
        os_log(OS_LOG_DEFAULT, "CH9344Transport TX completion failed 0x%x bytes %u expected %u", status, actualByteCount, ivars->txFrameLength);
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
    const bool abortStatus = ivars->statusInPending;
    IOLockUnlock(ivars->lock);

    if (ivars->usb.dataIn != nullptr && abortIn) {
        os_log(OS_LOG_DEFAULT, "CH9344Transport Abort IN begin");
        ivars->usb.dataIn->Abort(
            kIOUSBAbortSynchronous,
            kIOReturnAborted,
            nullptr);
        os_log(OS_LOG_DEFAULT, "CH9344Transport Abort IN end");
    }
    if (ivars->usb.dataOut != nullptr && abortOut) {
        os_log(OS_LOG_DEFAULT, "CH9344Transport Abort OUT begin");
        ivars->usb.dataOut->Abort(
            kIOUSBAbortSynchronous,
            kIOReturnAborted,
            nullptr);
        os_log(OS_LOG_DEFAULT, "CH9344Transport Abort OUT end");
    }
    if (ivars->usb.commandIn != nullptr && abortStatus) {
        os_log(OS_LOG_DEFAULT, "CH9344Transport Abort status IN begin");
        ivars->usb.commandIn->Abort(kIOUSBAbortSynchronous, kIOReturnAborted, nullptr);
        os_log(OS_LOG_DEFAULT, "CH9344Transport Abort status IN end");
    }

    IOLockLock(ivars->lock);
    ivars->dataInPending = false;
    ivars->dataOutPending = false;
    ivars->statusInPending = false;
    ivars->usb.asyncStatusReader = false;
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
