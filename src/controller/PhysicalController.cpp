#include "controller/PhysicalController.h"
#include "Logger.h"
#include <linux/input.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>

namespace {

// Full-scale output for a digital-ish source bound to an analog target:
// 255 for triggers, ±32767 for sticks.
int axisOutputValue(LogicalAxis axis) {
    bool isTrigger = (axis == LogicalAxis::LeftTrigger || axis == LogicalAxis::RightTrigger);
    return isTrigger ? 255 : 32767;
}

}  // namespace

PhysicalController::PhysicalController(std::unique_ptr<EventReader> reader,
                                        std::unique_ptr<VirtualController> vc,
                                        Mapping m)
    : eventReader(std::move(reader)),
      virtualController(std::move(vc)),
      mapping(std::move(m)) {}

void PhysicalController::setActive(bool on) {
    if (on && !active_) {
        if (eventReader->grab(true)) {
            active_ = true;
            Logger::info("Mapping active for controller: " + eventReader->deviceName());
        }
    } else if (!on && active_) {
        active_ = false;
        virtualController->reset();
        eventReader->grab(false);
        Logger::info("Mapping inactive for controller: " + eventReader->deviceName());
    }
}

bool PhysicalController::isActive() const {
    return active_.load();
}

void PhysicalController::run() {
    while (!stopRequested) {
        auto ev = eventReader->read();
        if (!ev) {
            if (eventReader->failed()) break;
            usleep(1000);
            continue;
        }
        processEvent(*ev);
    }
    captureCv.notify_all();  // wake up anyone waiting on a capture if we're shutting down
}

void PhysicalController::stop() {
    stopRequested = true;
    captureCv.notify_all();
}

CaptureResult PhysicalController::captureNext(int timeoutMs) {
    std::unique_lock<std::mutex> lock(captureMutex);
    captureResult = {};
    capturing = true;

    captureCv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
        [this] { return captureResult.kind != CaptureKind::None || stopRequested.load(); });

    capturing = false;
    return captureResult;   // kind stays None if we timed out — that's the "skip" signal
}

void PhysicalController::processEvent(const RawEvent& ev) {
    if (ev.type == EV_ABS) {
        // Deflection is tracked on every axis event, not just during capture windows,
        // so a release between prompts (returning to rest) resets the edge state and a
        // later genuine press can still capture instead of looking like a held axis.
        float deflCenter = std::fabs(eventReader->normalizeAxis(ev.code, ev.value));
        float deflMin    = eventReader->normalizeTrigger(ev.code, ev.value);

        auto it = lastAxisDeflection.find(ev.code);
        bool wasRestCenter = true, wasRestMin = true;
        if (it != lastAxisDeflection.end()) {
            wasRestCenter = it->second.first  < kAxisEdgeLow;
            wasRestMin    = it->second.second < kAxisEdgeLow;
        }
        lastAxisDeflection[ev.code] = { deflCenter, deflMin };

        if (capturing) {
            std::lock_guard<std::mutex> lock(captureMutex);
            // An axis at rest looks "deflected" under the wrong model (a resting trigger
            // is at its min, so center-normalization reads -1.0; a centered stick reads
            // ~0.5 under min-normalization). Accept a rising edge under either model.
            bool risingCenter = wasRestCenter && deflCenter >= kAxisEdgeHigh;
            bool risingMin    = wasRestMin    && deflMin    >= kAxisEdgeHigh;
            if (risingCenter || risingMin) {
                captureResult = { CaptureKind::Axis, ev.code, ev.value };
                captureCv.notify_one();
            }
            return;   // ignore everything else while capturing
        }

        if (!active_.load()) return;

        if (isHatAxisCode(ev.code)) {
            dispatchHatSource(ev.code, ev.value);
        } else {
            dispatchAnalogAxis(ev.code, ev.value);
        }
        return;
    }

    if (capturing) {
        std::lock_guard<std::mutex> lock(captureMutex);
        if (ev.type == EV_KEY && ev.value == 1) {
            captureResult = { CaptureKind::Button, ev.code, ev.value };
            captureCv.notify_one();
        }
        return;
    }

    if (!active_.load()) return;

    if (ev.type == EV_KEY) {
        dispatchButtonSource(ev.code, ev.value);
    }
}

// Digital source (button): presses/releases its digital targets and drives its
// analog targets at full-on / off (255 for a trigger, ±32767 for a stick).
void PhysicalController::dispatchButtonSource(int code, int state) {
    bool pressed = (state == 1);
    for (auto logical : mapping.controlTargets(buttonSource(code))) {
        virtualController->pressButton(logical, state);
    }
    for (auto axis : mapping.axisTargets(buttonSource(code))) {
        virtualController->setAxis(axis, pressed ? axisOutputValue(axis) : 0);
    }
}

// Hat (EV_ABS with -1/0/+1): a non-zero value presses one direction, zero releases both.
void PhysicalController::dispatchHatSource(int code, int value) {
    if (value == 0) {
        for (int dir : { -1, 1 }) {
            PhysicalSource src = hatSource(code, dir);
            for (auto logical : mapping.controlTargets(src)) virtualController->pressButton(logical, 0);
            for (auto axis : mapping.axisTargets(src)) virtualController->setAxis(axis, 0);
        }
        return;
    }
    int dir = (value > 0) ? 1 : -1;

    // The pressed direction's bindings: digital targets press, analog targets hit
    // full scale (the D-pad is a plain 4-button set, one per direction).
    PhysicalSource src = hatSource(code, dir);
    for (auto logical : mapping.controlTargets(src)) virtualController->pressButton(logical, 1);
    for (auto axis : mapping.axisTargets(src)) virtualController->setAxis(axis, axisOutputValue(axis));
}

// Analog axis: continuous values to analog targets; digital targets flip on a
// 0.5-threshold crossing (min of both deflection models = no false rest press).
void PhysicalController::dispatchAnalogAxis(int code, int rawValue) {
    float deflCenter = std::fabs(eventReader->normalizeAxis(code, rawValue));
    float deflMin    = eventReader->normalizeTrigger(code, rawValue);

    for (auto axis : mapping.axisTargets(axisSource(code))) {
        int virtValue;
        if (axis == LogicalAxis::LeftTrigger || axis == LogicalAxis::RightTrigger) {
            virtValue = static_cast<int>(deflMin * 255.0f);
        } else {
            float norm = eventReader->normalizeAxis(code, rawValue);        // -1.0..1.0
            virtValue = static_cast<int>(norm * 32767.0f);
        }
        virtualController->setAxis(axis, virtValue);
    }

    auto controls = mapping.controlTargets(axisSource(code));
    if (!controls.empty()) {
        float switchLevel = std::min(deflCenter, deflMin);
        bool nowPressed = switchLevel >= kSwitchEdge;
        bool& wasPressed = analogSwitchPressed[code];
        if (nowPressed && !wasPressed) {
            for (auto logical : controls) virtualController->pressButton(logical, 1);
        } else if (!nowPressed && wasPressed) {
            for (auto logical : controls) virtualController->pressButton(logical, 0);
        }
        wasPressed = nowPressed;
    }
}