#include "LibusbDevice.hpp"

#include <libusb.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr uint16_t kVendorId = 0x1a86;
constexpr uint16_t kProductId = 0xe018;
constexpr uint8_t kInterfaceNumber = 0;
constexpr unsigned int kTransferTimeoutMilliseconds = 5000;

std::string usbError(const char* operation, int result)
{
    return std::string(operation) + ": " + libusb_error_name(result);
}

ch9344_probe::DeviceResult sendBulkOut(
    libusb_device_handle* handle,
    uint8_t endpoint,
    const uint8_t* bytes,
    std::size_t length,
    std::string* errorMessage)
{
    int transferred = 0;
    const int result = libusb_bulk_transfer(
        handle,
        endpoint,
        const_cast<uint8_t*>(bytes),
        static_cast<int>(length),
        &transferred,
        kTransferTimeoutMilliseconds);
    if (result != LIBUSB_SUCCESS) {
        *errorMessage = usbError("Bulk OUT", result);
        return ch9344_probe::DeviceResult::transferError;
    }
    if (transferred != static_cast<int>(length)) {
        *errorMessage = "Bulk OUT 发生短写";
        return ch9344_probe::DeviceResult::transferError;
    }
    return ch9344_probe::DeviceResult::success;
}

ch9344_probe::DeviceResult sendCommandSequence(
    libusb_device_handle* handle,
    uint8_t endpoint,
    const ch9344::CommandSequence& sequence,
    std::string* errorMessage)
{
    for (std::size_t index = 0; index < sequence.count; ++index) {
        const ch9344_probe::DeviceResult result = sendBulkOut(
            handle,
            endpoint,
            sequence.commands[index].bytes,
            sequence.commands[index].length,
            errorMessage);
        if (result != ch9344_probe::DeviceResult::success) {
            return result;
        }
    }
    return ch9344_probe::DeviceResult::success;
}

ch9344_probe::DeviceResult drainInputEndpoint(
    libusb_device_handle* handle,
    uint8_t endpoint,
    uint16_t maxPacketSize,
    std::string* errorMessage)
{
    std::vector<uint8_t> buffer(maxPacketSize);
    for (unsigned int attempt = 0; attempt < 32; ++attempt) {
        int transferred = 0;
        const int result = libusb_bulk_transfer(
            handle,
            endpoint,
            buffer.data(),
            static_cast<int>(buffer.size()),
            &transferred,
            20);
        if (result == LIBUSB_ERROR_TIMEOUT) {
            return ch9344_probe::DeviceResult::success;
        }
        if (result != LIBUSB_SUCCESS) {
            *errorMessage = usbError("清空 Bulk IN", result);
            return ch9344_probe::DeviceResult::transferError;
        }
    }
    return ch9344_probe::DeviceResult::success;
}

ch9344_probe::DeviceResult sendCommandsAndDrainStatus(
    libusb_device_handle* handle,
    const ch9344::EndpointLayout& endpoints,
    const ch9344::CommandSequence& sequence,
    std::string* errorMessage)
{
    const ch9344_probe::DeviceResult sendResult = sendCommandSequence(
        handle, endpoints.commandOut, sequence, errorMessage);
    if (sendResult != ch9344_probe::DeviceResult::success) {
        return sendResult;
    }
    return drainInputEndpoint(
        handle,
        endpoints.commandIn,
        endpoints.commandMaxPacketSize,
        errorMessage);
}

struct RxAccumulator {
    uint8_t logicalPort;
    std::vector<uint8_t> bytes;
};

void accumulateTargetPort(void* context, const ch9344::RxRecordView& record)
{
    auto* accumulator = static_cast<RxAccumulator*>(context);
    if (record.logicalPort != accumulator->logicalPort) {
        return;
    }
    accumulator->bytes.insert(
        accumulator->bytes.end(),
        record.payload,
        record.payload + record.payloadLength);
}

ch9344_probe::DeviceResult sendPayload(
    libusb_device_handle* handle,
    const ch9344::EndpointLayout& endpoints,
    const ch9344_probe::LoopbackRequest& request,
    std::string* errorMessage)
{
    std::vector<uint8_t> frame(endpoints.dataMaxPacketSize);
    std::size_t sent = 0;
    while (sent < request.payloadLength) {
        const ch9344::TxFrameResult frameResult = ch9344::encodeTxFrame(
            request.logicalPort,
            request.payload + sent,
            request.payloadLength - sent,
            endpoints.dataMaxPacketSize,
            frame.data(),
            frame.size());
        if (frameResult.error != ch9344::Error::none
            || frameResult.payloadConsumed == 0) {
            *errorMessage = "TX 数据无法按 CH9344 协议组帧";
            return ch9344_probe::DeviceResult::loopbackError;
        }

        const ch9344_probe::DeviceResult sendResult = sendBulkOut(
            handle,
            endpoints.dataOut,
            frame.data(),
            frameResult.outputLength,
            errorMessage);
        if (sendResult != ch9344_probe::DeviceResult::success) {
            return sendResult;
        }
        sent += frameResult.payloadConsumed;
    }
    return ch9344_probe::DeviceResult::success;
}

ch9344_probe::DeviceResult receivePayload(
    libusb_device_handle* handle,
    const ch9344::EndpointLayout& endpoints,
    const ch9344_probe::LoopbackRequest& request,
    std::string* errorMessage)
{
    RxAccumulator accumulator {request.logicalPort, {}};
    accumulator.bytes.reserve(request.payloadLength);
    std::vector<uint8_t> input(endpoints.dataMaxPacketSize);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while (std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        const unsigned int timeout = static_cast<unsigned int>(
            std::max<int64_t>(1, std::min<int64_t>(remaining.count(), 250)));
        int transferred = 0;
        const int result = libusb_bulk_transfer(
            handle,
            endpoints.dataIn,
            input.data(),
            static_cast<int>(input.size()),
            &transferred,
            timeout);
        if (result == LIBUSB_ERROR_TIMEOUT) {
            continue;
        }
        if (result != LIBUSB_SUCCESS) {
            *errorMessage = usbError("Data Bulk IN", result);
            return ch9344_probe::DeviceResult::transferError;
        }

        const ch9344::DecodeResult decodeResult = ch9344::decodeRxTransfer(
            input.data(),
            static_cast<std::size_t>(transferred),
            accumulateTargetPort,
            &accumulator);
        if (decodeResult.error != ch9344::Error::none) {
            *errorMessage = "收到不合法的 CH9344 RX 记录";
            return ch9344_probe::DeviceResult::loopbackError;
        }
        if (accumulator.bytes.size() > request.payloadLength) {
            *errorMessage = "回环收到多于预期的字节";
            return ch9344_probe::DeviceResult::loopbackError;
        }
        if (accumulator.bytes.size() == request.payloadLength) {
            if (!std::equal(
                    accumulator.bytes.begin(),
                    accumulator.bytes.end(),
                    request.payload)) {
                *errorMessage = "回环数据内容不一致";
                return ch9344_probe::DeviceResult::loopbackError;
            }
            return ch9344_probe::DeviceResult::success;
        }
    }

    *errorMessage = "等待第 4 路回环数据超时";
    return ch9344_probe::DeviceResult::loopbackError;
}

} // namespace

ch9344_probe::LibusbDevice::~LibusbDevice()
{
    if (handle_ != nullptr) {
        if (interfaceClaimed_) {
            libusb_release_interface(handle_, kInterfaceNumber);
        }
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

ch9344_probe::DeviceResult ch9344_probe::LibusbDevice::loopback(
    const LoopbackRequest& request,
    std::string* errorMessage)
{
    if (errorMessage == nullptr || request.payload == nullptr
        || request.payloadLength == 0 || handle_ == nullptr) {
        return DeviceResult::loopbackError;
    }

    Inspection inspection {};
    DeviceResult operationResult = inspect(&inspection, errorMessage);
    if (operationResult != DeviceResult::success) {
        return operationResult;
    }

    const int autoDetachResult = libusb_set_auto_detach_kernel_driver(handle_, 1);
    if (autoDetachResult != LIBUSB_SUCCESS
        && autoDetachResult != LIBUSB_ERROR_NOT_SUPPORTED) {
        *errorMessage = usbError("libusb_set_auto_detach_kernel_driver", autoDetachResult);
        return DeviceResult::transferError;
    }

    const int claimResult = libusb_claim_interface(handle_, kInterfaceNumber);
    if (claimResult != LIBUSB_SUCCESS) {
        *errorMessage = usbError("libusb_claim_interface", claimResult);
        return DeviceResult::transferError;
    }
    interfaceClaimed_ = true;

    ch9344::CommandSequence sequence;
    if (ch9344::encodeDeviceInitialization(inspection.chip, &sequence)
        != ch9344::Error::none) {
        *errorMessage = "无法编码设备初始化命令";
        return DeviceResult::loopbackError;
    }
    operationResult = sendCommandsAndDrainStatus(
        handle_, inspection.endpoints, sequence, errorMessage);
    if (operationResult != DeviceResult::success) {
        return operationResult;
    }

    if (ch9344::encodePortInitialization(request.logicalPort, &sequence)
        != ch9344::Error::none) {
        *errorMessage = "无法编码端口初始化命令";
        return DeviceResult::loopbackError;
    }
    operationResult = sendCommandsAndDrainStatus(
        handle_, inspection.endpoints, sequence, errorMessage);
    if (operationResult != DeviceResult::success) {
        return operationResult;
    }

    if (ch9344::encodeUart8N1(
            inspection.chip.variant,
            request.logicalPort,
            request.baudRate,
            &sequence)
        != ch9344::Error::none) {
        *errorMessage = "无法编码 8N1 命令";
        return DeviceResult::loopbackError;
    }
    operationResult = sendCommandsAndDrainStatus(
        handle_, inspection.endpoints, sequence, errorMessage);
    if (operationResult != DeviceResult::success) {
        return operationResult;
    }

    if (ch9344::encodeModemControl(
            request.logicalPort, true, true, &sequence)
        != ch9344::Error::none) {
        *errorMessage = "无法编码 DTR/RTS 命令";
        return DeviceResult::loopbackError;
    }
    operationResult = sendCommandsAndDrainStatus(
        handle_, inspection.endpoints, sequence, errorMessage);
    const bool modemRaised = operationResult == DeviceResult::success;

    if (operationResult == DeviceResult::success) {
        operationResult = drainInputEndpoint(
            handle_,
            inspection.endpoints.dataIn,
            inspection.endpoints.dataMaxPacketSize,
            errorMessage);
    }
    if (operationResult == DeviceResult::success) {
        operationResult = sendPayload(
            handle_, inspection.endpoints, request, errorMessage);
    }
    if (operationResult == DeviceResult::success) {
        operationResult = receivePayload(
            handle_, inspection.endpoints, request, errorMessage);
    }

    if (modemRaised
        && ch9344::encodeModemControl(
               request.logicalPort, false, false, &sequence)
            == ch9344::Error::none) {
        std::string ignoredError;
        sendCommandSequence(
            handle_, inspection.endpoints.commandOut, sequence, &ignoredError);
    }
    return operationResult;
}
