#pragma once

#include <cstddef>
#include <cstdint>

#include <ch9344/Protocol.hpp>

namespace ch9344::driver {

class UsbTransactionBackend {
public:
    virtual ~UsbTransactionBackend() = default;

    virtual bool openInterface() = 0;
    virtual bool acquirePipe(std::uint8_t address) = 0;
    virtual bool readChipVersion(
        std::uint8_t* output,
        std::size_t capacity,
        std::size_t* actualLength) = 0;
    virtual bool writeCommand(
        const std::uint8_t* bytes,
        std::size_t length,
        std::size_t* actualLength) = 0;
    virtual bool drainCommandStatus() = 0;
    virtual void releasePipe(std::uint8_t address) = 0;
    virtual void closeInterface() = 0;
};

enum class UsbStartupError {
    none,
    openFailed,
    pipeFailed,
    versionTransferFailed,
    invalidVersion,
    commandTransferFailed,
    protocolError,
};

struct UsbStartupResult {
    UsbStartupError error;
    ch9344::ChipInfo chip;
};

UsbStartupResult initializeUsbTransport(
    UsbTransactionBackend& backend,
    std::uint8_t logicalPort,
    std::uint32_t defaultBaudRate);

bool submitCommandSequence(
    UsbTransactionBackend& backend,
    const ch9344::CommandSequence& sequence);

void shutdownUsbTransport(UsbTransactionBackend& backend);

} // namespace ch9344::driver
