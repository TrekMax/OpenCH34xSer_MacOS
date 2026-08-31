#include <DriverKit/IOBufferMemoryDescriptor.h>
#include <DriverKit/IOLib.h>
#include <DriverKit/IOMemoryDescriptor.h>
#include <DriverKit/IOMemoryMap.h>
#include <SerialDriverKit/SerialPortInterface.h>

#include "../Shared/DriverCore.hpp"
#include "CH9344Driver.h"
#include "CH9344Transport.h"

struct CH9344Driver_IVars {
    CH9344Transport* transport;
    driverkit::serial::SerialPortInterface* serialInterface;
    IOMemoryMap* interfaceMap;
    IOMemoryMap* rxMap;
    IOMemoryMap* txMap;
    std::uint8_t* rxRing;
    std::uint8_t* txRing;
    std::uint8_t logicalPort;
    bool configured;
    bool queuesConnected;
    bool active;
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
    ivars->transport = OSDynamicCast(CH9344Transport, provider);
    if (ivars->transport == nullptr) {
        Stop(provider, SUPERDISPATCH);
        return kIOReturnBadArgument;
    }
    return kIOReturnSuccess;
}

kern_return_t IMPL(CH9344Driver, Stop)
{
    if (ivars != nullptr) {
        ivars->active = false;
        ReleaseQueueMappings();
        ivars->transport = nullptr;
    }
    return Stop(provider, SUPERDISPATCH);
}

void CH9344Driver::free()
{
    if (ivars != nullptr) {
        ReleaseQueueMappings();
        IOSafeDeleteNULL(ivars, CH9344Driver_IVars, 1);
    }
    super::free();
}

bool CH9344Driver::ConfigurePort(std::uint8_t logicalPort)
{
    if (ivars == nullptr || ivars->configured || logicalPort >= 4) {
        return false;
    }
    ivars->logicalPort = logicalPort;
    ivars->configured = true;
    return true;
}

kern_return_t IMPL(CH9344Driver, ConnectQueues)
{
    if (ivars == nullptr || !ivars->configured) {
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
    if (ivars->transport != nullptr) {
        ivars->transport->NotifyRxSpace(ivars->logicalPort);
        ivars->transport->NotifyTx(ivars->logicalPort);
    }
    return kIOReturnSuccess;
}

kern_return_t IMPL(CH9344Driver, DisconnectQueues)
{
    ReleaseQueueMappings();
    return DisconnectQueues(SUPERDISPATCH);
}

void IMPL(CH9344Driver, TxDataAvailable)
{
    if (ivars != nullptr && ivars->transport != nullptr) {
        ivars->transport->NotifyTx(ivars->logicalPort);
    }
}

void IMPL(CH9344Driver, RxFreeSpaceAvailable)
{
    if (ivars != nullptr && ivars->transport != nullptr) {
        ivars->transport->NotifyRxSpace(ivars->logicalPort);
    }
}

kern_return_t IMPL(CH9344Driver, HwActivate)
{
    if (ivars == nullptr || !ivars->configured || ivars->transport == nullptr ||
        !ivars->transport->IsTransportReady()) {
        return kIOReturnNotReady;
    }
    ivars->active = true;
    ivars->transport->NotifyRxSpace(ivars->logicalPort);
    ivars->transport->NotifyTx(ivars->logicalPort);
    return kIOReturnSuccess;
}

kern_return_t IMPL(CH9344Driver, HwDeactivate)
{
    if (ivars != nullptr) {
        ivars->active = false;
    }
    return kIOReturnSuccess;
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
    if (ivars == nullptr || ivars->transport == nullptr || !ivars->configured) {
        return kIOReturnNotReady;
    }
    return ivars->transport->ProgramUART(
        ivars->logicalPort,
        baudRate,
        nDataBits,
        nHalfStopBits,
        parity);
}

kern_return_t IMPL(CH9344Driver, HwProgramBaudRate)
{
    if (ivars == nullptr || ivars->transport == nullptr || !ivars->configured) {
        return kIOReturnNotReady;
    }
    return ivars->transport->ProgramUART(
        ivars->logicalPort,
        baudRate,
        8,
        ch9344::driver::kOneStopBitInHalfBits,
        ch9344::driver::kParityNone);
}

kern_return_t IMPL(CH9344Driver, HwProgramMCR)
{
    if (ivars == nullptr || ivars->transport == nullptr || !ivars->configured) {
        return kIOReturnNotReady;
    }
    return ivars->transport->ProgramModemControl(
        ivars->logicalPort, dtr, rts);
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

bool CH9344Driver::HasPendingTx()
{
    if (ivars == nullptr || !ivars->active || !ivars->queuesConnected ||
        ivars->serialInterface == nullptr || ivars->txRing == nullptr) {
        return false;
    }
    const std::uint32_t producer = __atomic_load_n(
        &ivars->serialInterface->txPI,
        __ATOMIC_ACQUIRE);
    const std::uint32_t consumer = __atomic_load_n(
        &ivars->serialInterface->txCI,
        __ATOMIC_ACQUIRE);
    return producer != consumer;
}

bool CH9344Driver::PrepareTx(
    std::uint8_t* output,
    std::uint32_t outputCapacity,
    std::uint32_t* frameLength,
    std::uint32_t* nextConsumer)
{
    if (frameLength == nullptr || nextConsumer == nullptr || !HasPendingTx()) {
        return false;
    }
    const std::uint32_t producer = __atomic_load_n(
        &ivars->serialInterface->txPI,
        __ATOMIC_ACQUIRE);
    const std::uint32_t consumer = __atomic_load_n(
        &ivars->serialInterface->txCI,
        __ATOMIC_ACQUIRE);
    const auto prepared = ch9344::driver::prepareTxTransfer(
        ivars->logicalPort,
        ivars->txRing,
        ivars->serialInterface->txqlogsz,
        producer,
        consumer,
        outputCapacity,
        output,
        outputCapacity);
    if (prepared.error != ch9344::driver::DriverCoreError::none ||
        prepared.frameLength == 0) {
        return false;
    }
    *frameLength = static_cast<std::uint32_t>(prepared.frameLength);
    *nextConsumer = prepared.nextConsumerIndex;
    return true;
}

void CH9344Driver::CompleteTx(
    bool transferSucceeded,
    std::uint32_t expectedLength,
    std::uint32_t actualLength,
    std::uint32_t candidateConsumer)
{
    if (ivars == nullptr || ivars->serialInterface == nullptr) {
        return;
    }
    const std::uint32_t currentConsumer = __atomic_load_n(
        &ivars->serialInterface->txCI,
        __ATOMIC_ACQUIRE);
    const auto completion = ch9344::driver::completeTxTransfer(
        ivars->active,
        transferSucceeded,
        expectedLength,
        actualLength,
        currentConsumer,
        candidateConsumer);
    if (!completion.committed) {
        return;
    }
    __atomic_store_n(
        &ivars->serialInterface->txCI,
        completion.nextConsumerIndex,
        __ATOMIC_RELEASE);
    TxFreeSpaceAvailable();
}

std::int32_t CH9344Driver::DeliverRx(
    const std::uint8_t* transfer,
    std::uint32_t transferLength)
{
    if (ivars == nullptr || !ivars->active || !ivars->queuesConnected ||
        ivars->serialInterface == nullptr || ivars->rxRing == nullptr) {
        return static_cast<std::int32_t>(ch9344::driver::DriverCoreError::none);
    }
    const std::uint32_t producer = __atomic_load_n(
        &ivars->serialInterface->rxPI,
        __ATOMIC_ACQUIRE);
    const std::uint32_t consumer = __atomic_load_n(
        &ivars->serialInterface->rxCI,
        __ATOMIC_ACQUIRE);
    const auto delivered = ch9344::driver::deliverRxTransfer(
        ivars->logicalPort,
        transfer,
        transferLength,
        ivars->rxRing,
        ivars->serialInterface->rxqlogsz,
        producer,
        consumer);
    if (delivered.error == ch9344::driver::DriverCoreError::none &&
        delivered.bytesWritten != 0) {
        __atomic_store_n(
            &ivars->serialInterface->rxPI,
            delivered.nextProducerIndex,
            __ATOMIC_RELEASE);
        RxDataAvailable();
    }
    return static_cast<std::int32_t>(delivered.error);
}

void CH9344Driver::ReportRxProtocolError()
{
    if (ivars != nullptr && ivars->active) {
        RxError(false, false, true, false);
    }
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
