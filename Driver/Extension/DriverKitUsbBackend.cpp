#include "DriverKitUsbBackend.hpp"

#include <DriverKit/IOLib.h>

#include <cstring>

namespace {

constexpr std::size_t kCommandBufferCapacity = 512;
constexpr std::uint32_t kTransferTimeoutMilliseconds = 5000;
constexpr std::uint32_t kDrainTimeoutMilliseconds = 20;
constexpr std::size_t kMaximumDrainTransfers = 32;

} // namespace

DriverKitUsbBackend::DriverKitUsbBackend(DriverKitUsbState* state)
    : state_(state)
{
}

bool DriverKitUsbBackend::openInterface()
{
    if (state_ == nullptr || state_->owner == nullptr || state_->interface == nullptr ||
        state_->opened) {
        return false;
    }
    if (state_->interface->Open(state_->owner, 0, nullptr) != kIOReturnSuccess) {
        return false;
    }
    state_->opened = true;

    if (state_->interface->CreateIOBuffer(
            kIOMemoryDirectionOutIn,
            kCommandBufferCapacity,
            &state_->commandBuffer) != kIOReturnSuccess ||
        state_->commandBuffer == nullptr) {
        return false;
    }
    if (state_->commandBuffer->GetAddressRange(&state_->commandRange) !=
            kIOReturnSuccess ||
        state_->commandRange.address == 0 ||
        state_->commandRange.length < kCommandBufferCapacity) {
        return false;
    }
    return true;
}

bool DriverKitUsbBackend::acquirePipe(std::uint8_t address)
{
    if (state_ == nullptr || state_->interface == nullptr || !state_->opened) {
        return false;
    }
    IOUSBHostPipe** slot = pipeSlot(address);
    if (slot == nullptr || *slot != nullptr) {
        return false;
    }
    return state_->interface->CopyPipe(address, slot) == kIOReturnSuccess &&
        *slot != nullptr;
}

bool DriverKitUsbBackend::readChipVersion(
    std::uint8_t* output,
    std::size_t capacity,
    std::size_t* actualLength)
{
    if (state_ == nullptr || state_->interface == nullptr ||
        state_->commandBuffer == nullptr || output == nullptr ||
        actualLength == nullptr || capacity < 4) {
        return false;
    }
    if (state_->commandBuffer->SetLength(4) != kIOReturnSuccess) {
        return false;
    }

    std::uint8_t* bytes = commandBytes();
    if (bytes == nullptr) {
        return false;
    }
    std::memset(bytes, 0, 4);
    std::uint16_t transferred = 0;
    const kern_return_t result = state_->interface->DeviceRequest(
        0xc0,
        0x96,
        0,
        0,
        4,
        state_->commandBuffer,
        &transferred,
        kTransferTimeoutMilliseconds);
    if (result != kIOReturnSuccess) {
        return false;
    }
    *actualLength = transferred;
    if (transferred <= capacity) {
        std::memcpy(output, bytes, transferred);
    }
    return true;
}

bool DriverKitUsbBackend::writeCommand(
    const std::uint8_t* bytes,
    std::size_t length,
    std::size_t* actualLength)
{
    if (state_ == nullptr || state_->commandOut == nullptr ||
        state_->commandBuffer == nullptr || bytes == nullptr ||
        actualLength == nullptr || length == 0 ||
        length > kCommandBufferCapacity) {
        return false;
    }
    if (state_->commandBuffer->SetLength(length) != kIOReturnSuccess) {
        return false;
    }
    std::uint8_t* destination = commandBytes();
    if (destination == nullptr) {
        return false;
    }
    std::memcpy(destination, bytes, length);

    std::uint32_t transferred = 0;
    const kern_return_t result = state_->commandOut->IO(
        state_->commandBuffer,
        static_cast<std::uint32_t>(length),
        &transferred,
        kTransferTimeoutMilliseconds);
    *actualLength = transferred;
    return result == kIOReturnSuccess;
}

bool DriverKitUsbBackend::drainCommandStatus()
{
    if (state_ == nullptr || state_->commandIn == nullptr ||
        state_->commandBuffer == nullptr) {
        return false;
    }
    for (std::size_t attempt = 0; attempt < kMaximumDrainTransfers; ++attempt) {
        if (state_->commandBuffer->SetLength(kCommandBufferCapacity) !=
            kIOReturnSuccess) {
            return false;
        }
        std::uint32_t transferred = 0;
        const kern_return_t result = state_->commandIn->IO(
            state_->commandBuffer,
            kCommandBufferCapacity,
            &transferred,
            kDrainTimeoutMilliseconds);
        if (result == kIOReturnTimeout) {
            return true;
        }
        if (result != kIOReturnSuccess) {
            return false;
        }
    }
    return true;
}

void DriverKitUsbBackend::releasePipe(std::uint8_t address)
{
    IOUSBHostPipe** slot = pipeSlot(address);
    if (slot != nullptr) {
        OSSafeReleaseNULL(*slot);
    }
}

void DriverKitUsbBackend::closeInterface()
{
    if (state_ == nullptr) {
        return;
    }
    releasePipe(0x02);
    releasePipe(0x01);
    releasePipe(0x82);
    releasePipe(0x81);
    OSSafeReleaseNULL(state_->commandBuffer);
    state_->commandRange = {};
    if (state_->opened && state_->interface != nullptr && state_->owner != nullptr) {
        state_->interface->Close(state_->owner, 0);
    }
    state_->opened = false;
}

IOUSBHostPipe** DriverKitUsbBackend::pipeSlot(std::uint8_t address)
{
    if (state_ == nullptr) {
        return nullptr;
    }
    switch (address) {
    case 0x81:
        return &state_->commandIn;
    case 0x82:
        return &state_->dataIn;
    case 0x01:
        return &state_->commandOut;
    case 0x02:
        return &state_->dataOut;
    default:
        return nullptr;
    }
}

std::uint8_t* DriverKitUsbBackend::commandBytes() const
{
    if (state_ == nullptr || state_->commandRange.address == 0) {
        return nullptr;
    }
    return reinterpret_cast<std::uint8_t*>(state_->commandRange.address);
}
