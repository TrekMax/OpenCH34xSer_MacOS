#pragma once

#include <DriverKit/IOBufferMemoryDescriptor.h>
#include <USBDriverKit/IOUSBHostInterface.h>
#include <USBDriverKit/IOUSBHostPipe.h>

#include "../Shared/UsbTransaction.hpp"

struct DriverKitUsbState {
    IOService* owner;
    IOUSBHostInterface* interface;
    IOUSBHostPipe* commandIn;
    IOUSBHostPipe* dataIn;
    IOUSBHostPipe* commandOut;
    IOUSBHostPipe* dataOut;
    IOBufferMemoryDescriptor* commandBuffer;
    IOAddressSegment commandRange;
    bool opened;
};

class DriverKitUsbBackend final : public ch9344::driver::UsbTransactionBackend {
public:
    explicit DriverKitUsbBackend(DriverKitUsbState* state);

    bool openInterface() override;
    bool acquirePipe(std::uint8_t address) override;
    bool readChipVersion(
        std::uint8_t* output,
        std::size_t capacity,
        std::size_t* actualLength) override;
    bool writeCommand(
        const std::uint8_t* bytes,
        std::size_t length,
        std::size_t* actualLength) override;
    bool drainCommandStatus() override;
    void releasePipe(std::uint8_t address) override;
    void closeInterface() override;

private:
    IOUSBHostPipe** pipeSlot(std::uint8_t address);
    std::uint8_t* commandBytes() const;

    DriverKitUsbState* state_;
};
