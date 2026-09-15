#pragma once
// Shared reader/controller fixtures for hardware-free unit tests.
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <linux/input.h>
#include "input/EventReader.h"
#include "virtual/VirtualController.h"

class IdleEventReader : public EventReader {
public:
    bool open(const std::string&) override { return true; }
    std::optional<RawEvent> read() override { return std::nullopt; }
    float normalizeAxis(int, int) const override { return 0.0f; }
    float normalizeTrigger(int, int) const override { return 0.0f; }
    std::string deviceName() const override { return "fake"; }
    const char* codeName(int, int) const override { return "CODE"; }
    bool failed() const override { return false; }
    std::vector<DeviceCapability> listButtonCapabilities() const override { return {}; }
    std::vector<DeviceCapability> listAxisCapabilities() const override { return {}; }
    bool grab(bool) override { return true; }
    DeviceIdentity identity() const override { return { "fake", 0x0FFF, 0x0001, "", "" }; }
};

// Replays a vector of RawEvents with configurable sleep between reads.
// After the sequence is exhausted returns nullopt.
// normalizeAxis returns a fixed value when called with the axis code in the replayed events.
class ScriptedEventReader : public EventReader {
public:
    ScriptedEventReader(std::vector<RawEvent> sequence,
                        float axisNormOverride = 0.0f,
                        std::chrono::milliseconds gapMs = std::chrono::milliseconds(20))
        : sequence(std::move(sequence)),
          axisNormOverride(axisNormOverride),
          gapMs(gapMs) {}

    bool open(const std::string&) override { return true; }

    std::optional<RawEvent> read() override {
        if (index >= sequence.size()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return std::nullopt;
        }
        std::this_thread::sleep_for(gapMs);
        return sequence[index++];
    }

    float normalizeAxis(int code, int rawValue) const override {
        if (axisNormOverride != 0.0f) return axisNormOverride;
        const int CENTER = 128;   // models the 0..255 center-based joystick (rest ~= 128)
        float n = static_cast<float>(rawValue - CENTER) / 128.0f;
        if (n > 1.0f) n = 1.0f;
        if (n < -1.0f) n = -1.0f;
        return n;
    }

    float normalizeTrigger(int code, int rawValue) const override {
        (void)code;
        if (rawValue < 0) return 0.0f;
        if (rawValue > 255) return 1.0f;
        return static_cast<float>(rawValue) / 255.0f;
    }

    std::string deviceName() const override { return "scripted"; }
    const char* codeName(int type, int code) const override {
        (void)type; (void)code;
        return "CODE";
    }
    bool failed() const override { return false; }

    std::vector<DeviceCapability> listButtonCapabilities() const override {
        return { { BTN_SOUTH, "BTN_SOUTH" } };
    }
    std::vector<DeviceCapability> listAxisCapabilities() const override {
        return { { ABS_X, "ABS_X" }, { ABS_HAT0Y, "ABS_HAT0Y" } };
    }

    bool grab(bool exclusive) override {
        grabCalls.push_back(exclusive);
        return true;
    }
    DeviceIdentity identity() const override { return identity_; }

    std::vector<bool> grabCalls;
    DeviceIdentity identity_{ "scripted", 0x0FFF, 0x0001, "", "" };

private:
    std::vector<RawEvent> sequence;
    size_t index = 0;
    float axisNormOverride;
    std::chrono::milliseconds gapMs;
};

class CountingVirtualController : public VirtualController {
public:
    std::vector<std::pair<LogicalAxis, int>> axisUpdates;
    std::vector<std::pair<LogicalControl, int>> buttonUpdates;
    int resetCalls = 0;

    bool create() override { return true; }
    int pressButton(LogicalControl control, int value) override {
        buttonUpdates.push_back({ control, value });
        return 1;
    }
    void setAxis(LogicalAxis axis, int value) override { axisUpdates.push_back({ axis, value }); }
    void reset() override { ++resetCalls; }
};