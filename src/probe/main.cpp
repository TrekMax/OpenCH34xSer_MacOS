#include "LibusbDevice.hpp"

#include <cstdint>
#include <cerrno>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

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
    case ch9344_probe::DeviceResult::loopbackError:
        return 6;
    }
    return 4;
}

bool parseUnsigned(const char* text, uint32_t* value)
{
    if (text == nullptr || value == nullptr || *text == '\0') {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0'
        || parsed > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    *value = static_cast<uint32_t>(parsed);
    return true;
}

int hexNibble(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

bool parseHex(const std::string& text, std::vector<uint8_t>* bytes)
{
    if (bytes == nullptr || text.empty() || text.size() % 2 != 0) {
        return false;
    }
    bytes->clear();
    bytes->reserve(text.size() / 2);
    for (std::size_t index = 0; index < text.size(); index += 2) {
        const int high = hexNibble(text[index]);
        const int low = hexNibble(text[index + 1]);
        if (high < 0 || low < 0) {
            bytes->clear();
            return false;
        }
        bytes->push_back(static_cast<uint8_t>((high << 4) | low));
    }
    return true;
}

void printUsage()
{
    std::cerr
        << "用法:\n"
        << "  ch9344-probe inspect\n"
        << "  ch9344-probe loopback --port 4 --baud 115200 --payload-hex <hex>\n"
        << "  ch9344-probe loopback --port 4 --baud 115200 --length <count>\n";
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
    if (argc == 2 && std::string(argv[1]) == "inspect") {
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
            "data-in",
            inspection.endpoints.dataIn,
            inspection.endpoints.dataMaxPacketSize);
        printEndpoint(
            "data-out",
            inspection.endpoints.dataOut,
            inspection.endpoints.dataMaxPacketSize);
        printEndpoint(
            "command-in",
            inspection.endpoints.commandIn,
            inspection.endpoints.commandMaxPacketSize);
        printEndpoint(
            "command-out",
            inspection.endpoints.commandOut,
            inspection.endpoints.commandMaxPacketSize);
        std::cout << "chip CH9344"
                  << (inspection.chip.variant == ch9344::ChipVariant::ch9344Q
                          ? 'Q'
                          : 'L')
                  << " version 0x" << std::hex << std::setw(2)
                  << std::setfill('0')
                  << static_cast<unsigned>(inspection.chip.version) << '\n';
        return 0;
    }

    if (argc != 8 || std::string(argv[1]) != "loopback"
        || std::string(argv[2]) != "--port"
        || std::string(argv[4]) != "--baud") {
        printUsage();
        return 2;
    }

    uint32_t userPort = 0;
    uint32_t baudRate = 0;
    if (!parseUnsigned(argv[3], &userPort) || userPort < 1 || userPort > 4
        || !parseUnsigned(argv[5], &baudRate) || baudRate == 0) {
        printUsage();
        return 2;
    }

    std::vector<uint8_t> payload;
    if (std::string(argv[6]) == "--payload-hex") {
        if (!parseHex(argv[7], &payload)) {
            printUsage();
            return 2;
        }
    } else if (std::string(argv[6]) == "--length") {
        uint32_t length = 0;
        if (!parseUnsigned(argv[7], &length) || length == 0) {
            printUsage();
            return 2;
        }
        payload.resize(length);
        for (std::size_t index = 0; index < payload.size(); ++index) {
            payload[index] = static_cast<uint8_t>((index * 37 + 0xa5) & 0xff);
        }
    } else {
        printUsage();
        return 2;
    }

    std::string errorMessage;
    ch9344_probe::LibusbDevice device;
    ch9344_probe::DeviceResult result = device.open(&errorMessage);
    if (result != ch9344_probe::DeviceResult::success) {
        std::cerr << errorMessage << '\n';
        return exitCode(result);
    }

    const ch9344_probe::LoopbackRequest request {
        static_cast<uint8_t>(userPort - 1),
        baudRate,
        payload.data(),
        payload.size(),
    };
    result = device.loopback(request, &errorMessage);
    if (result != ch9344_probe::DeviceResult::success) {
        std::cerr << errorMessage << '\n';
        return exitCode(result);
    }

    std::cout << "PASS port=" << userPort << " baud=" << baudRate
              << " bytes=" << payload.size() << '\n';
    return 0;
}
