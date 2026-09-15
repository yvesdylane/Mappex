#pragma once
#include "virtual/OutputBackend.h"
#include "virtual/VirtualController.h"
#include <vector>

struct AxisCapability {
    int code;
    int minValue;
    int maxValue;
};

class UInputBackend : public OutputBackend {
public:
    ~UInputBackend() override;

    bool create() override;
    bool create(const VirtualDeviceInfo& info) override;
    void reset() override;
    void sendButton(int code, int value) override;
    void sendAxis(int code, int value) override;
    void addButtonCapability(int code) override;
    void addAxisCapability(int code, int minValue, int maxValue) override;

    // The /dev/input/eventN node this device was created as, or "" if it isn't
    // resolvable yet. Used to recognize our own virtual devices at hot-plug time.
    std::string devicePath() const;

private:
    int fd = -1;
    std::vector<int> buttonCodes;
    std::vector<AxisCapability> axisCodes;
    std::string deviceName_;
    uint16_t vendorId_ = kSpoofedVendorId;
    uint16_t productId_ = kSpoofedProductId;
    uint16_t version_ = 1;
};