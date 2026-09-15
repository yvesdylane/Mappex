#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <chrono>
#include <unordered_map>
#include <utility>
#include "input/EventReader.h"
#include "virtual/VirtualController.h"
#include "controller/Mapping.h"

enum class CaptureKind { None, Button, Axis };

struct CaptureResult {
    CaptureKind kind = CaptureKind::None;
    int code = -1;
    int value = 0;   // raw axis value; hat direction (the ±1) when it came from a hat
};

class PhysicalController {
public:
    PhysicalController(std::unique_ptr<EventReader> reader,
                       std::unique_ptr<VirtualController> virtualController,
                       Mapping mapping);

    void run();
    void stop();

    // Exclusive grab + virtual output on/off. Deactivate releases the grab and
    // parks every button/axis so nothing stays pressed in games.
    void setActive(bool on);
    bool isActive() const;

    Mapping& mapping_ref() { return mapping; }

    std::string codeName(int type, int code) const { return eventReader->codeName(type, code); }

    // Physical events this device can emit (drives the GUI's source dropdowns).
    std::vector<DeviceCapability> eventReaderCapsButtons() const {
        return eventReader->listButtonCapabilities();
    }
    std::vector<DeviceCapability> eventReaderCapsAxes() const {
        return eventReader->listAxisCapabilities();
    }

    // The identity the kernel reports for this device.
    DeviceIdentity identity() const { return eventReader->identity(); }

    // Waits up to timeoutMs for either a button press or a significant axis movement.
    // Returns kind=None if timeout expires or stop() is called.
    CaptureResult captureNext(int timeoutMs);

private:
    void processEvent(const RawEvent& ev);
    void dispatchButtonSource(int code, int state);
    void dispatchHatSource(int code, int value);
    void dispatchAnalogAxis(int code, int rawValue);

    std::unique_ptr<EventReader> eventReader;
    std::unique_ptr<VirtualController> virtualController;
    Mapping mapping;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> active_{false};

    std::atomic<bool> capturing{false};
    std::mutex captureMutex;
    std::condition_variable captureCv;
    CaptureResult captureResult;

    // Per-axis last deflection under both known axis semantics, kept across capture
    // windows so a held/lingering axis never reads as a fresh press:
    //   .first  = center-based deflection (sticks/joysticks: rest = middle)
    //   .second = min-based deflection from normalizeTrigger (analog triggers: rest = min)
    // A real press requires a rising edge (was below kAxisEdgeLow, now at/above kAxisEdgeHigh).
    std::unordered_map<int, std::pair<float, float>> lastAxisDeflection;
    static constexpr float kAxisEdgeLow  = 0.30f;
    static constexpr float kAxisEdgeHigh = 0.52f;

    // Analog axis -> digital control derivation: last crossing state per axis. The
    // crossing level uses min(center, min) deflection so a resting trigger (center
    // model says "fully deflected") or a centered stick (min model says ~0.5) can't
    // read as a false press.
    std::unordered_map<int, bool> analogSwitchPressed;
    static constexpr float kSwitchEdge = 0.50f;
};