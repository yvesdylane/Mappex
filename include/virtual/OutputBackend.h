#pragma once
#include <cstdint>
#include <string>
#include "controller/DeviceIdentity.h"

struct VirtualDeviceInfo {
    std::string name;
    uint16_t vendorId = 0;
    uint16_t productId = 0;
    uint16_t version = 1;
};

class OutputBackend {
public:
    virtual ~OutputBackend() = default;

    virtual void addButtonCapability(int code) = 0;
    virtual void addAxisCapability(int code, int minValue, int maxValue) = 0;

    virtual bool create() = 0;

    // Identity-aware creation. Default delegates to the no-arg create().
    virtual bool create(const VirtualDeviceInfo&) { return create(); }

    // Sends all buttons to 0 and axes to their centre/default value.
    // Default is a no-op; override to make your virtual device "safe to park".
    virtual void reset() {}

    // The /dev/input/eventN node this device was created as, or "" if unknown.
    virtual std::string devicePath() const { return {}; }

    virtual void sendButton(int code, int value) = 0;
    virtual void sendAxis(int code, int value) = 0;
};