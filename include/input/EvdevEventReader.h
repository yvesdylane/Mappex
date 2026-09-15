#pragma once
#include "input/EventReader.h"
#include <string>
#include <vector>

struct libevdev;

class EvdevEventReader : public EventReader {
public:
    ~EvdevEventReader() override;

    bool open(const std::string& devicePath) override;
    std::optional<RawEvent> read() override;
    float normalizeAxis(int code, int rawValue) const override;
    float normalizeTrigger(int code, int rawValue) const override;
    std::string deviceName() const override;
    const char* codeName(int type, int code) const override;
    bool failed() const override;
    std::vector<DeviceCapability> listButtonCapabilities() const override;
    std::vector<DeviceCapability> listAxisCapabilities() const override;
    bool grab(bool exclusive) override;
    DeviceIdentity identity() const override;

private:
    int fd = -1;
    libevdev* dev = nullptr;
    bool readFailed = false;
};