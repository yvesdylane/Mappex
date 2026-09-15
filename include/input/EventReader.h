#pragma once
#include <optional>
#include <string>
#include <vector>
#include "input/RawEvent.h"
#include "controller/DeviceIdentity.h"

struct DeviceCapability {
    int code;
    std::string name;
};

class EventReader {
public:
    virtual ~EventReader() = default;

    virtual bool open(const std::string& devicePath) = 0;
    virtual std::optional<RawEvent> read() = 0;
    virtual float normalizeAxis(int code, int rawValue) const = 0;      // -1.0..1.0, for sticks
    virtual float normalizeTrigger(int code, int rawValue) const = 0;   // 0.0..1.0, for triggers
    virtual std::string deviceName() const = 0;
    virtual const char* codeName(int type, int code) const = 0;
    virtual bool failed() const = 0;

    // Physical events this device can actually emit — drives the GUI's source list.
    virtual std::vector<DeviceCapability> listButtonCapabilities() const = 0;
    virtual std::vector<DeviceCapability> listAxisCapabilities() const = 0;

    // Exclusive grab (EVIOCGRAB): while held, this process is the only receiver
    // of the device's events — X11, Wayland, Steam and other apps see nothing.
    virtual bool grab(bool exclusive) = 0;

    // Vendor/product/serial the kernel reports for the device.
    virtual DeviceIdentity identity() const = 0;
};