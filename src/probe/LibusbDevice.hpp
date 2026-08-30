#pragma once

#include <ch9344/EndpointLayout.hpp>
#include <ch9344/Protocol.hpp>

#include <string>

struct libusb_context;
struct libusb_device_handle;

namespace ch9344_probe {

enum class DeviceResult {
    success,
    openError,
    descriptorError,
    transferError,
};

struct Inspection {
    ch9344::EndpointLayout endpoints;
    ch9344::ChipInfo chip;
};

class LibusbDevice {
public:
    LibusbDevice() = default;
    ~LibusbDevice();

    LibusbDevice(const LibusbDevice&) = delete;
    LibusbDevice& operator=(const LibusbDevice&) = delete;

    DeviceResult open(std::string* errorMessage);
    DeviceResult inspect(Inspection* inspection, std::string* errorMessage);

private:
    libusb_context* context_ = nullptr;
    libusb_device_handle* handle_ = nullptr;
};

} // namespace ch9344_probe
