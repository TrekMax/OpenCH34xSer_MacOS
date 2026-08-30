#include "UsbTransaction.hpp"

#include <array>

namespace {

constexpr std::array<std::uint8_t, 4> kRequiredPipes {{
    0x81,
    0x82,
    0x01,
    0x02,
}};

void rollback(
    ch9344::driver::UsbTransactionBackend& backend,
    std::size_t acquiredPipeCount)
{
    while (acquiredPipeCount > 0) {
        --acquiredPipeCount;
        backend.releasePipe(kRequiredPipes[acquiredPipeCount]);
    }
    backend.closeInterface();
}

bool sendSequence(
    ch9344::driver::UsbTransactionBackend& backend,
    const ch9344::CommandSequence& sequence)
{
    for (std::size_t index = 0; index < sequence.count; ++index) {
        const ch9344::Command& command = sequence.commands[index];
        std::size_t actualLength = 0;
        if (!backend.writeCommand(
                command.bytes,
                command.length,
                &actualLength) ||
            actualLength != command.length) {
            return false;
        }
    }
    return sequence.count == 0 || backend.drainCommandStatus();
}

} // namespace

ch9344::driver::UsbStartupResult ch9344::driver::initializeUsbTransport(
    UsbTransactionBackend& backend,
    std::uint8_t logicalPort,
    std::uint32_t defaultBaudRate)
{
    const ch9344::ChipInfo emptyChip {ch9344::ChipVariant::ch9344L, 0};
    if (!backend.openInterface()) {
        return {UsbStartupError::openFailed, emptyChip};
    }

    std::size_t acquiredPipeCount = 0;
    for (const std::uint8_t address : kRequiredPipes) {
        if (!backend.acquirePipe(address)) {
            rollback(backend, acquiredPipeCount);
            return {UsbStartupError::pipeFailed, emptyChip};
        }
        ++acquiredPipeCount;
    }

    std::uint8_t versionBytes[4] {};
    std::size_t versionLength = 0;
    if (!backend.readChipVersion(
            versionBytes,
            sizeof(versionBytes),
            &versionLength)) {
        rollback(backend, acquiredPipeCount);
        return {UsbStartupError::versionTransferFailed, emptyChip};
    }

    ch9344::ChipInfo chip {};
    if (ch9344::parseChipVersion(versionBytes, versionLength, &chip) !=
        ch9344::Error::none) {
        rollback(backend, acquiredPipeCount);
        return {UsbStartupError::invalidVersion, emptyChip};
    }

    ch9344::CommandSequence sequence;
    if (ch9344::encodeDeviceInitialization(chip, &sequence) !=
            ch9344::Error::none ||
        !sendSequence(backend, sequence)) {
        rollback(backend, acquiredPipeCount);
        return {UsbStartupError::commandTransferFailed, chip};
    }
    if (ch9344::encodePortInitialization(logicalPort, &sequence) !=
        ch9344::Error::none) {
        rollback(backend, acquiredPipeCount);
        return {UsbStartupError::protocolError, chip};
    }
    if (!sendSequence(backend, sequence)) {
        rollback(backend, acquiredPipeCount);
        return {UsbStartupError::commandTransferFailed, chip};
    }
    if (ch9344::encodeUart8N1(
            chip.variant,
            logicalPort,
            defaultBaudRate,
            &sequence) != ch9344::Error::none) {
        rollback(backend, acquiredPipeCount);
        return {UsbStartupError::protocolError, chip};
    }
    if (!sendSequence(backend, sequence)) {
        rollback(backend, acquiredPipeCount);
        return {UsbStartupError::commandTransferFailed, chip};
    }
    if (ch9344::encodeModemControl(
            logicalPort,
            false,
            false,
            &sequence) != ch9344::Error::none) {
        rollback(backend, acquiredPipeCount);
        return {UsbStartupError::protocolError, chip};
    }
    if (!sendSequence(backend, sequence)) {
        rollback(backend, acquiredPipeCount);
        return {UsbStartupError::commandTransferFailed, chip};
    }

    return {UsbStartupError::none, chip};
}

bool ch9344::driver::submitCommandSequence(
    UsbTransactionBackend& backend,
    const ch9344::CommandSequence& sequence)
{
    return sendSequence(backend, sequence);
}

void ch9344::driver::shutdownUsbTransport(UsbTransactionBackend& backend)
{
    rollback(backend, kRequiredPipes.size());
}
