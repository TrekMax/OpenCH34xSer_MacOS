#include "../../Driver/Shared/UsbTransaction.hpp"
#include "../TestSupport.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

namespace {

class FakeUsbBackend final : public ch9344::driver::UsbTransactionBackend {
public:
    bool openResult = true;
    std::uint8_t failedPipe = 0;
    bool shortVersion = false;
    std::size_t shortCommandIndex = 0;

    std::vector<std::uint8_t> acquired;
    std::vector<std::uint8_t> released;
    std::vector<std::array<std::uint8_t, 16>> commands;
    std::vector<std::size_t> commandLengths;
    std::size_t drainCount = 0;
    bool opened = false;
    bool closed = false;

    bool openInterface() override
    {
        opened = openResult;
        return openResult;
    }

    bool acquirePipe(std::uint8_t address) override
    {
        if (address == failedPipe) {
            return false;
        }
        acquired.push_back(address);
        return true;
    }

    bool readChipVersion(
        std::uint8_t* output,
        std::size_t capacity,
        std::size_t* actualLength) override
    {
        if (output == nullptr || actualLength == nullptr || capacity < 4) {
            return false;
        }
        output[0] = 0x42;
        output[1] = 0x11;
        output[2] = 0x22;
        output[3] = 0x33;
        *actualLength = shortVersion ? 3 : 4;
        return true;
    }

    bool writeCommand(
        const std::uint8_t* bytes,
        std::size_t length,
        std::size_t* actualLength) override
    {
        std::array<std::uint8_t, 16> captured {};
        for (std::size_t index = 0; index < length; ++index) {
            captured[index] = bytes[index];
        }
        commands.push_back(captured);
        commandLengths.push_back(length);
        *actualLength = shortCommandIndex == commands.size()
            ? length - 1
            : length;
        return true;
    }

    bool drainCommandStatus() override
    {
        ++drainCount;
        return true;
    }

    void releasePipe(std::uint8_t address) override
    {
        released.push_back(address);
    }

    void closeInterface() override
    {
        closed = true;
    }
};

void checkAddresses(
    const std::vector<std::uint8_t>& actual,
    const std::uint8_t* expected,
    std::size_t expectedCount)
{
    CHECK_EQ(actual.size(), expectedCount);
    if (actual.size() == expectedCount) {
        CHECK_BYTES(actual.data(), expected, expectedCount);
    }
}

void testInitializesQChipAndKeepsTransportOpen()
{
    FakeUsbBackend backend;
    const auto result = ch9344::driver::initializeUsbTransport(backend, 3, 115200);
    const std::uint8_t expectedPipes[] = {0x81, 0x82, 0x01, 0x02};
    const std::uint8_t expectedUpload[] = {0x94, 0x9d, 0x01, 0, 0, 0, 0, 0};

    CHECK_EQ(result.error, ch9344::driver::UsbStartupError::none);
    CHECK_EQ(result.chip.variant, ch9344::ChipVariant::ch9344Q);
    CHECK_EQ(result.chip.version, 0x42);
    CHECK_EQ(backend.opened, true);
    CHECK_EQ(backend.closed, false);
    checkAddresses(backend.acquired, expectedPipes, std::size(expectedPipes));
    CHECK_EQ(backend.commands.size(), 12U);
    CHECK_EQ(backend.commandLengths[0], sizeof(expectedUpload));
    CHECK_BYTES(backend.commands[0].data(), expectedUpload, sizeof(expectedUpload));
    CHECK_EQ(backend.drainCount, 4U);

    ch9344::driver::shutdownUsbTransport(backend);
    const std::uint8_t expectedRelease[] = {0x02, 0x01, 0x82, 0x81};
    checkAddresses(backend.released, expectedRelease, std::size(expectedRelease));
    CHECK_EQ(backend.closed, true);
}

void testRollsBackAcquiredPipesWhenLastPipeFails()
{
    FakeUsbBackend backend;
    backend.failedPipe = 0x02;

    const auto result = ch9344::driver::initializeUsbTransport(backend, 3, 115200);
    const std::uint8_t expectedRelease[] = {0x01, 0x82, 0x81};

    CHECK_EQ(result.error, ch9344::driver::UsbStartupError::pipeFailed);
    checkAddresses(backend.released, expectedRelease, std::size(expectedRelease));
    CHECK_EQ(backend.closed, true);
    CHECK_EQ(backend.commands.size(), 0U);
}

void testInitializesAllFourPortsOnOneSharedTransport()
{
    FakeUsbBackend backend;
    const auto result = ch9344::driver::initializeAllPortsUsbTransport(
        backend, 115200);

    CHECK_EQ(result.error, ch9344::driver::UsbStartupError::none);
    CHECK_EQ(result.chip.variant, ch9344::ChipVariant::ch9344Q);
    CHECK_EQ(backend.commands.size(), 45U);
    CHECK_EQ(backend.drainCount, 13U);
    CHECK_EQ(backend.commands[1][1], 0x0a);
    CHECK_EQ(backend.commands[12][1], 0x1a);
    CHECK_EQ(backend.commands[23][1], 0x2a);
    CHECK_EQ(backend.commands[34][1], 0x3a);
    CHECK_EQ(backend.closed, false);

    ch9344::driver::shutdownUsbTransport(backend);
    CHECK_EQ(backend.closed, true);
}

void testRejectsShortVersionAndRollsBackAllPipes()
{
    FakeUsbBackend backend;
    backend.shortVersion = true;

    const auto result = ch9344::driver::initializeUsbTransport(backend, 3, 115200);
    const std::uint8_t expectedRelease[] = {0x02, 0x01, 0x82, 0x81};

    CHECK_EQ(result.error, ch9344::driver::UsbStartupError::invalidVersion);
    checkAddresses(backend.released, expectedRelease, std::size(expectedRelease));
    CHECK_EQ(backend.closed, true);
    CHECK_EQ(backend.commands.size(), 0U);
}

void testRejectsShortCommandBeforeDrainingAndRollsBack()
{
    FakeUsbBackend backend;
    backend.shortCommandIndex = 1;

    const auto result = ch9344::driver::initializeUsbTransport(backend, 3, 115200);
    const std::uint8_t expectedRelease[] = {0x02, 0x01, 0x82, 0x81};

    CHECK_EQ(result.error, ch9344::driver::UsbStartupError::commandTransferFailed);
    CHECK_EQ(backend.commands.size(), 1U);
    CHECK_EQ(backend.drainCount, 0U);
    checkAddresses(backend.released, expectedRelease, std::size(expectedRelease));
    CHECK_EQ(backend.closed, true);
}

void testSubmitsRuntimeCommandSequenceWithExactLengths()
{
    FakeUsbBackend backend;
    ch9344::CommandSequence sequence;
    CHECK_EQ(
        ch9344::encodeModemControl(3, true, true, &sequence),
        ch9344::Error::none);

    CHECK_EQ(
        ch9344::driver::submitCommandSequence(backend, sequence),
        true);
    CHECK_EQ(backend.commands.size(), 2U);
    CHECK_EQ(backend.commandLengths[0], 3U);
    CHECK_EQ(backend.commandLengths[1], 3U);
    CHECK_EQ(backend.drainCount, 1U);

    FakeUsbBackend shortBackend;
    shortBackend.shortCommandIndex = 2;
    CHECK_EQ(
        ch9344::driver::submitCommandSequence(shortBackend, sequence),
        false);
    CHECK_EQ(shortBackend.commands.size(), 2U);
    CHECK_EQ(shortBackend.drainCount, 0U);
}

} // namespace

int main()
{
    testInitializesQChipAndKeepsTransportOpen();
    testInitializesAllFourPortsOnOneSharedTransport();
    testRollsBackAcquiredPipesWhenLastPipeFails();
    testRejectsShortVersionAndRollsBackAllPipes();
    testRejectsShortCommandBeforeDrainingAndRollsBack();
    testSubmitsRuntimeCommandSequenceWithExactLengths();
    return test_support::failures == 0 ? 0 : 1;
}
