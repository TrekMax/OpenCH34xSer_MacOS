#include "LibusbDevice.hpp"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

int exitCode(ch9344_probe::DeviceResult result)
{
    switch (result) {
    case ch9344_probe::DeviceResult::success:
        return 0;
    case ch9344_probe::DeviceResult::openError:
        return 3;
    case ch9344_probe::DeviceResult::descriptorError:
        return 4;
    case ch9344_probe::DeviceResult::transferError:
        return 5;
    }
    return 4;
}

void printEndpoint(const char* name, uint8_t address, uint16_t maxPacketSize)
{
    std::cout << name << " 0x" << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<unsigned>(address) << std::dec << " bulk "
              << maxPacketSize << '\n';
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 2 || std::string(argv[1]) != "inspect") {
        std::cerr << "用法: ch9344-probe inspect\n";
        return 2;
    }

    std::string errorMessage;
    ch9344_probe::LibusbDevice device;
    ch9344_probe::DeviceResult result = device.open(&errorMessage);
    if (result != ch9344_probe::DeviceResult::success) {
        std::cerr << errorMessage << '\n';
        return exitCode(result);
    }

    ch9344_probe::Inspection inspection {};
    result = device.inspect(&inspection, &errorMessage);
    if (result != ch9344_probe::DeviceResult::success) {
        std::cerr << errorMessage << '\n';
        return exitCode(result);
    }

    std::cout << "device 1a86:e018\n";
    std::cout << "interface 0\n";
    printEndpoint(
        "data-in", inspection.endpoints.dataIn, inspection.endpoints.dataMaxPacketSize);
    printEndpoint(
        "data-out", inspection.endpoints.dataOut, inspection.endpoints.dataMaxPacketSize);
    printEndpoint(
        "command-in",
        inspection.endpoints.commandIn,
        inspection.endpoints.commandMaxPacketSize);
    printEndpoint(
        "command-out",
        inspection.endpoints.commandOut,
        inspection.endpoints.commandMaxPacketSize);
    std::cout << "chip CH9344"
              << (inspection.chip.variant == ch9344::ChipVariant::ch9344Q ? 'Q' : 'L')
              << " version 0x" << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<unsigned>(inspection.chip.version) << '\n';
    return 0;
}
