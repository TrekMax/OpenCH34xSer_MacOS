#include "LibusbDevice.hpp"

#include <libusb.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace {

constexpr uint16_t kVendorId = 0x1a86;
constexpr uint16_t kProductId = 0xe018;
constexpr uint8_t kInterfaceNumber = 0;
constexpr unsigned int kTransferTimeoutMilliseconds = 5000;

std::string usbError(const char* operation, int result)
{
    return std::string(operation) + ": " + libusb_error_name(result);
}

} // namespace

ch9344_probe::LibusbDevice::~LibusbDevice()
{
    if (handle_ != nullptr) {
        libusb_close(handle_);
    }
    if (context_ != nullptr) {
        libusb_exit(context_);
    }
}

ch9344_probe::DeviceResult ch9344_probe::LibusbDevice::open(
    std::string* errorMessage)
{
    if (errorMessage == nullptr) {
        return DeviceResult::openError;
    }

    const int initResult = libusb_init(&context_);
    if (initResult != LIBUSB_SUCCESS) {
        *errorMessage = usbError("libusb_init", initResult);
        return DeviceResult::openError;
    }

    handle_ = libusb_open_device_with_vid_pid(context_, kVendorId, kProductId);
    if (handle_ == nullptr) {
        *errorMessage = "未找到或无法打开 USB 设备 1a86:e018";
        return DeviceResult::openError;
    }
    return DeviceResult::success;
}

ch9344_probe::DeviceResult ch9344_probe::LibusbDevice::inspect(
    Inspection* inspection,
    std::string* errorMessage)
{
    if (inspection == nullptr || errorMessage == nullptr || handle_ == nullptr) {
        return DeviceResult::descriptorError;
    }

    libusb_device* device = libusb_get_device(handle_);
    libusb_device_descriptor deviceDescriptor {};
    int result = libusb_get_device_descriptor(device, &deviceDescriptor);
    if (result != LIBUSB_SUCCESS) {
        *errorMessage = usbError("libusb_get_device_descriptor", result);
        return DeviceResult::descriptorError;
    }
    if (deviceDescriptor.idVendor != kVendorId
        || deviceDescriptor.idProduct != kProductId) {
        *errorMessage = "打开的设备 USB ID 与 1a86:e018 不一致";
        return DeviceResult::descriptorError;
    }

    libusb_config_descriptor* rawConfiguration = nullptr;
    result = libusb_get_config_descriptor(device, 0, &rawConfiguration);
    if (result != LIBUSB_SUCCESS) {
        *errorMessage = usbError("libusb_get_config_descriptor", result);
        return DeviceResult::descriptorError;
    }
    const std::unique_ptr<libusb_config_descriptor, decltype(&libusb_free_config_descriptor)>
        configuration(rawConfiguration, &libusb_free_config_descriptor);

    const libusb_interface_descriptor* targetInterface = nullptr;
    for (uint8_t index = 0; index < configuration->bNumInterfaces; ++index) {
        const libusb_interface& interface = configuration->interface[index];
        for (int alternate = 0; alternate < interface.num_altsetting; ++alternate) {
            const libusb_interface_descriptor& descriptor = interface.altsetting[alternate];
            if (descriptor.bInterfaceNumber == kInterfaceNumber
                && descriptor.bAlternateSetting == 0) {
                targetInterface = &descriptor;
            }
        }
    }
    if (targetInterface == nullptr || targetInterface->bNumEndpoints != 4) {
        *errorMessage = "interface 0 不包含预期的四个端点";
        return DeviceResult::descriptorError;
    }

    std::array<ch9344::EndpointDescriptor, 4> endpointDescriptors {};
    for (std::size_t index = 0; index < endpointDescriptors.size(); ++index) {
        const libusb_endpoint_descriptor& endpoint = targetInterface->endpoint[index];
        endpointDescriptors[index] = {
            endpoint.bEndpointAddress,
            (endpoint.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK)
                    == LIBUSB_TRANSFER_TYPE_BULK
                ? ch9344::EndpointType::bulk
                : ch9344::EndpointType::other,
            endpoint.wMaxPacketSize,
        };
    }

    ch9344::EndpointLayout layout {};
    if (ch9344::classifyEndpoints(
            endpointDescriptors.data(), endpointDescriptors.size(), &layout)
        != ch9344::Error::none) {
        *errorMessage = "interface 0 的端点布局不是 CH9344 预期格式";
        return DeviceResult::descriptorError;
    }

    std::array<uint8_t, 4> versionBytes {};
    result = libusb_control_transfer(
        handle_,
        0xc0,
        0x96,
        0,
        0,
        versionBytes.data(),
        static_cast<uint16_t>(versionBytes.size()),
        kTransferTimeoutMilliseconds);
    if (result < 0) {
        *errorMessage = usbError("读取芯片版本", result);
        return DeviceResult::transferError;
    }
    if (result != static_cast<int>(versionBytes.size())) {
        *errorMessage = "芯片版本响应不是 4 字节";
        return DeviceResult::descriptorError;
    }

    ch9344::ChipInfo chip {};
    if (ch9344::parseChipVersion(versionBytes.data(), versionBytes.size(), &chip)
        != ch9344::Error::none) {
        *errorMessage = "芯片版本响应无法解析";
        return DeviceResult::descriptorError;
    }

    *inspection = {layout, chip};
    return DeviceResult::success;
}
