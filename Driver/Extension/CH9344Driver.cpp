#include <DriverKit/IOLib.h>
#include <DriverKit/IOMemoryMap.h>
#include <DriverKit/IOUserServer.h>
#include <SerialDriverKit/SerialPortInterface.h>
#include <USBDriverKit/AppleUSBDefinitions.h>
#include <USBDriverKit/USBDriverKitDefs.h>

#include "../Shared/DriverCore.hpp"
#include "../Shared/UsbTransaction.hpp"
#include "CH9344Driver.h"
#include "DriverKitUsbBackend.hpp"

namespace {

constexpr std::uint8_t kPrototypeLogicalPort = 3;
constexpr std::uint32_t kDataBufferCapacity = 512;
constexpr std::uint32_t kDataTransferTimeoutMilliseconds = 5000;

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

struct CH9344Driver_IVars {
    DriverKitUsbState usb;
    ch9344::ChipInfo chip;
    driverkit::serial::SerialPortInterface* serialInterface;
    IOMemoryMap* interfaceMap;
    IOMemoryMap* rxMap;
    IOMemoryMap* txMap;
    std::uint8_t* rxRing;
    std::uint8_t* txRing;
    IOBufferMemoryDescriptor* dataInBuffer;
    IOBufferMemoryDescriptor* dataOutBuffer;
    IOAddressSegment dataInRange;
    IOAddressSegment dataOutRange;
    OSAction* dataInAction;
    OSAction* dataOutAction;
    std::uint32_t rxTransferLength;
    std::uint32_t txFrameLength;
    std::uint32_t txNextConsumer;
    bool queuesConnected;
    bool transportReady;
    bool active;
    bool dataInPending;
    bool dataOutPending;
    bool rxTransferWaiting;
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
    return RegisterService();
}

kern_return_t IMPL(CH9344Driver, Stop)
{
    if (ivars != nullptr) {
        DeactivateTransport();
        ReleaseQueueMappings();
    }
    return Stop(provider, SUPERDISPATCH);
}

void CH9344Driver::free()
{
    if (ivars != nullptr) {
        ReleaseDataResources();
        ReleaseQueueMappings();
        OSSafeReleaseNULL(ivars->usb.dataOut);
        OSSafeReleaseNULL(ivars->usb.commandOut);
        OSSafeReleaseNULL(ivars->usb.dataIn);
        OSSafeReleaseNULL(ivars->usb.commandIn);
        OSSafeReleaseNULL(ivars->usb.commandBuffer);
        IOSafeDeleteNULL(ivars, CH9344Driver_IVars, 1);
    }
    super::free();
}

kern_return_t IMPL(CH9344Driver, ConnectQueues)
{
    if (ivars == nullptr) {
        return kIOReturnNotReady;
    }

    ReleaseQueueMappings();
    kern_return_t result = ConnectQueues(
        ifmd,
        rxqmd,
        txqmd,
        in_rxqmd,
        in_txqmd,
        in_rxqoffset,
        in_txqoffset,
        in_rxqlogsz,
        in_txqlogsz,
        SUPERDISPATCH);
    if (result != kIOReturnSuccess) {
        return result;
    }
    if (ifmd == nullptr || rxqmd == nullptr || txqmd == nullptr ||
        *ifmd == nullptr || *rxqmd == nullptr || *txqmd == nullptr) {
        DisconnectQueues(SUPERDISPATCH);
        return kIOReturnBadArgument;
    }

    result = (*ifmd)->CreateMapping(0, 0, 0, 0, 0, &ivars->interfaceMap);
    if (result == kIOReturnSuccess) {
        result = (*rxqmd)->CreateMapping(0, 0, 0, 0, 0, &ivars->rxMap);
    }
    if (result == kIOReturnSuccess) {
        result = (*txqmd)->CreateMapping(0, 0, 0, 0, 0, &ivars->txMap);
    }
    if (result != kIOReturnSuccess || ivars->interfaceMap == nullptr ||
        ivars->rxMap == nullptr || ivars->txMap == nullptr ||
        ivars->interfaceMap->GetLength() < sizeof(driverkit::serial::SerialPortInterface)) {
        ReleaseQueueMappings();
        DisconnectQueues(SUPERDISPATCH);
        return result == kIOReturnSuccess ? kIOReturnNoMemory : result;
    }

    ivars->serialInterface = reinterpret_cast<driverkit::serial::SerialPortInterface*>(
        ivars->interfaceMap->GetAddress());
    if (ivars->serialInterface == nullptr ||
        ivars->serialInterface->rxqlogsz < 1 ||
        ivars->serialInterface->rxqlogsz > 30 ||
        ivars->serialInterface->txqlogsz < 1 ||
        ivars->serialInterface->txqlogsz > 30) {
        ReleaseQueueMappings();
        DisconnectQueues(SUPERDISPATCH);
        return kIOReturnBadArgument;
    }

    const std::uint64_t rxRingSize = 1ULL << ivars->serialInterface->rxqlogsz;
    const std::uint64_t txRingSize = 1ULL << ivars->serialInterface->txqlogsz;
    if (ivars->serialInterface->rxqoffset > ivars->rxMap->GetLength() ||
        rxRingSize > ivars->rxMap->GetLength() - ivars->serialInterface->rxqoffset ||
        ivars->serialInterface->txqoffset > ivars->txMap->GetLength() ||
        txRingSize > ivars->txMap->GetLength() - ivars->serialInterface->txqoffset) {
        ReleaseQueueMappings();
        DisconnectQueues(SUPERDISPATCH);
        return kIOReturnBadArgument;
    }

    ivars->rxRing = reinterpret_cast<std::uint8_t*>(ivars->rxMap->GetAddress()) +
        ivars->serialInterface->rxqoffset;
    ivars->txRing = reinterpret_cast<std::uint8_t*>(ivars->txMap->GetAddress()) +
        ivars->serialInterface->txqoffset;
    ivars->queuesConnected = true;
    if (ivars->active) {
        SubmitDataIn();
        StartTx();
    }
    return kIOReturnSuccess;
}

kern_return_t IMPL(CH9344Driver, DisconnectQueues)
{
    if (ivars != nullptr) {
        ivars->queuesConnected = false;
        ivars->rxTransferWaiting = false;
        if (ivars->usb.dataIn != nullptr && ivars->dataInPending) {
            ivars->usb.dataIn->Abort(
                kIOUSBAbortSynchronous,
                kIOReturnAborted,
                nullptr);
        }
        if (ivars->usb.dataOut != nullptr && ivars->dataOutPending) {
            ivars->usb.dataOut->Abort(
                kIOUSBAbortSynchronous,
                kIOReturnAborted,
                nullptr);
        }
        ivars->dataInPending = false;
        ivars->dataOutPending = false;
        ReleaseQueueMappings();
    }
    return DisconnectQueues(SUPERDISPATCH);
}

kern_return_t IMPL(CH9344Driver, HwActivate)
{
    return ActivateTransport();
}

kern_return_t IMPL(CH9344Driver, HwDeactivate)
{
    DeactivateTransport();
    return kIOReturnSuccess;
}

void IMPL(CH9344Driver, TxDataAvailable)
{
    StartTx();
}

void IMPL(CH9344Driver, RxFreeSpaceAvailable)
{
    DeliverPendingRx();
    if (ivars != nullptr && !ivars->rxTransferWaiting) {
        SubmitDataIn();
    }
}

void CH9344Driver::StartTx()
{
    if (ivars == nullptr || !ivars->active || !ivars->transportReady ||
        !ivars->queuesConnected || ivars->serialInterface == nullptr ||
        ivars->txRing == nullptr || ivars->dataOutBuffer == nullptr ||
        ivars->usb.dataOut == nullptr || ivars->dataOutAction == nullptr ||
        ivars->dataOutPending) {
        return;
    }

    const std::uint32_t producer = __atomic_load_n(
        &ivars->serialInterface->txPI,
        __ATOMIC_ACQUIRE);
    const std::uint32_t consumer = __atomic_load_n(
        &ivars->serialInterface->txCI,
        __ATOMIC_ACQUIRE);
    auto* frame = reinterpret_cast<std::uint8_t*>(ivars->dataOutRange.address);
    const auto prepared = ch9344::driver::prepareTxTransfer(
        kPrototypeLogicalPort,
        ivars->txRing,
        ivars->serialInterface->txqlogsz,
        producer,
        consumer,
        kDataBufferCapacity,
        frame,
        ivars->dataOutRange.length);
    if (prepared.error != ch9344::driver::DriverCoreError::none ||
        prepared.frameLength == 0) {
        return;
    }
    if (ivars->dataOutBuffer->SetLength(prepared.frameLength) != kIOReturnSuccess) {
        return;
    }

    ivars->txFrameLength = static_cast<std::uint32_t>(prepared.frameLength);
    ivars->txNextConsumer = prepared.nextConsumerIndex;
    ivars->dataOutPending = true;
    const kern_return_t result = ivars->usb.dataOut->AsyncIO(
        ivars->dataOutBuffer,
        ivars->txFrameLength,
        ivars->dataOutAction,
        kDataTransferTimeoutMilliseconds);
    if (result != kIOReturnSuccess) {
        ivars->dataOutPending = false;
    }
}

void CH9344Driver::SubmitDataIn()
{
    if (ivars == nullptr || !ivars->active || !ivars->transportReady ||
        !ivars->queuesConnected || ivars->rxTransferWaiting ||
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

void CH9344Driver::DeliverPendingRx()
{
    if (ivars == nullptr || !ivars->rxTransferWaiting ||
        !ivars->queuesConnected || ivars->serialInterface == nullptr ||
        ivars->rxRing == nullptr) {
        return;
    }

    const std::uint32_t producer = __atomic_load_n(
        &ivars->serialInterface->rxPI,
        __ATOMIC_ACQUIRE);
    const std::uint32_t consumer = __atomic_load_n(
        &ivars->serialInterface->rxCI,
        __ATOMIC_ACQUIRE);
    const auto delivered = ch9344::driver::deliverRxTransfer(
        kPrototypeLogicalPort,
        reinterpret_cast<const std::uint8_t*>(ivars->dataInRange.address),
        ivars->rxTransferLength,
        ivars->rxRing,
        ivars->serialInterface->rxqlogsz,
        producer,
        consumer);
    if (delivered.error == ch9344::driver::DriverCoreError::rxBackpressure) {
        return;
    }

    ivars->rxTransferWaiting = false;
    if (delivered.error != ch9344::driver::DriverCoreError::none) {
        RxError(false, false, true, false);
        SubmitDataIn();
        return;
    }
    if (delivered.bytesWritten != 0) {
        __atomic_store_n(
            &ivars->serialInterface->rxPI,
            delivered.nextProducerIndex,
            __ATOMIC_RELEASE);
        RxDataAvailable();
    }
    SubmitDataIn();
}

void IMPL(CH9344Driver, DataInComplete)
{
    (void)action;
    (void)completionTimestamp;
    if (ivars == nullptr) {
        return;
    }
    ivars->dataInPending = false;
    if (!ivars->active) {
        return;
    }
    if (status != kIOReturnSuccess || actualByteCount > kDataBufferCapacity) {
        RxError(false, false, status == kIOReturnSuccess, false);
        return;
    }

    ivars->rxTransferLength = actualByteCount;
    ivars->rxTransferWaiting = true;
    DeliverPendingRx();
}

void IMPL(CH9344Driver, DataOutComplete)
{
    (void)action;
    (void)completionTimestamp;
    if (ivars == nullptr) {
        return;
    }
    ivars->dataOutPending = false;
    if (ivars->serialInterface == nullptr) {
        return;
    }

    const std::uint32_t currentConsumer = __atomic_load_n(
        &ivars->serialInterface->txCI,
        __ATOMIC_ACQUIRE);
    const auto completion = ch9344::driver::completeTxTransfer(
        ivars->active,
        status == kIOReturnSuccess,
        ivars->txFrameLength,
        actualByteCount,
        currentConsumer,
        ivars->txNextConsumer);
    if (!completion.committed) {
        return;
    }
    __atomic_store_n(
        &ivars->serialInterface->txCI,
        completion.nextConsumerIndex,
        __ATOMIC_RELEASE);
    TxFreeSpaceAvailable();
    StartTx();
}

kern_return_t CH9344Driver::ActivateTransport()
{
    if (ivars == nullptr || ivars->usb.interface == nullptr) {
        return kIOReturnNotReady;
    }
    if (ivars->active) {
        return kIOReturnSuccess;
    }

    DriverKitUsbBackend backend(&ivars->usb);
    const auto startup = ch9344::driver::initializeUsbTransport(
        backend,
        kPrototypeLogicalPort,
        115200);
    kern_return_t result = mapStartupError(startup.error);
    if (result != kIOReturnSuccess) {
        ch9344::driver::shutdownUsbTransport(backend);
        return result;
    }

    result = ivars->usb.interface->CreateIOBuffer(
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
        ch9344::driver::shutdownUsbTransport(backend);
        return result;
    }

    ivars->chip = startup.chip;
    ivars->transportReady = true;
    ivars->active = true;
    SubmitDataIn();
    StartTx();
    return kIOReturnSuccess;
}

void CH9344Driver::DeactivateTransport()
{
    if (ivars == nullptr) {
        return;
    }
    ivars->active = false;
    if (ivars->usb.dataIn != nullptr && ivars->dataInPending) {
        ivars->usb.dataIn->Abort(
            kIOUSBAbortSynchronous,
            kIOReturnAborted,
            nullptr);
    }
    if (ivars->usb.dataOut != nullptr && ivars->dataOutPending) {
        ivars->usb.dataOut->Abort(
            kIOUSBAbortSynchronous,
            kIOReturnAborted,
            nullptr);
    }
    ivars->dataInPending = false;
    ivars->dataOutPending = false;
    ivars->rxTransferWaiting = false;
    ReleaseDataResources();

    if (ivars->usb.opened) {
        DriverKitUsbBackend backend(&ivars->usb);
        ch9344::driver::shutdownUsbTransport(backend);
    }
    ivars->transportReady = false;
}

void CH9344Driver::ReleaseDataResources()
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

void CH9344Driver::ReleaseQueueMappings()
{
    if (ivars == nullptr) {
        return;
    }
    ivars->serialInterface = nullptr;
    ivars->rxRing = nullptr;
    ivars->txRing = nullptr;
    ivars->queuesConnected = false;
    OSSafeReleaseNULL(ivars->txMap);
    OSSafeReleaseNULL(ivars->rxMap);
    OSSafeReleaseNULL(ivars->interfaceMap);
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
    if (ivars == nullptr || !ivars->transportReady) {
        return kIOReturnNotReady;
    }
    ch9344::CommandSequence commands;
    const kern_return_t result = mapConfigurationError(
        ch9344::driver::buildUartConfiguration(
        ivars->chip.variant,
        kPrototypeLogicalPort,
        baudRate,
        nDataBits,
        nHalfStopBits,
        parity,
        &commands));
    if (result != kIOReturnSuccess) {
        return result;
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
