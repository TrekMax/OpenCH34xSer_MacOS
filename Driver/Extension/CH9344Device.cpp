#include <DriverKit/IOLib.h>
#include <USBDriverKit/IOUSBHostDevice.h>
#include <os/log.h>

#include "CH9344Device.h"

struct CH9344Device_IVars {
    IOUSBHostDevice* device;
    bool opened;
};

bool CH9344Device::init()
{
    if (!super::init()) {
        return false;
    }
    ivars = IONewZero(CH9344Device_IVars, 1);
    return ivars != nullptr;
}

kern_return_t IMPL(CH9344Device, Start)
{
    kern_return_t result = Start(provider, SUPERDISPATCH);
    if (result != kIOReturnSuccess) {
        return result;
    }
    ivars->device = OSDynamicCast(IOUSBHostDevice, provider);
    if (ivars->device == nullptr) {
        return kIOReturnBadArgument;
    }
    result = ivars->device->Open(this, 0, 0);
    if (result == kIOReturnSuccess) {
        ivars->opened = true;
        // Publish interface 0 so CH9344Transport can match and own its pipes.
        result = ivars->device->SetConfiguration(1, true);
    }
    os_log(OS_LOG_DEFAULT, "CH9344Device configuration 1: 0x%x", result);
    if (result == kIOReturnSuccess) {
        result = RegisterService();
    }
    if (result != kIOReturnSuccess) {
        CloseDevice();
    }
    return result;
}

void CH9344Device::CloseDevice()
{
    if (ivars != nullptr && ivars->opened) {
        ivars->device->Close(this, 0);
        ivars->opened = false;
    }
    if (ivars != nullptr) {
        ivars->device = nullptr;
    }
}

kern_return_t IMPL(CH9344Device, Stop)
{
    CloseDevice();
    return Stop(provider, SUPERDISPATCH);
}

void CH9344Device::free()
{
    CloseDevice();
    IOSafeDeleteNULL(ivars, CH9344Device_IVars, 1);
    super::free();
}
