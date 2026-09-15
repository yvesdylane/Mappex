#pragma once
// Shared helpers for user-facing flows (terminal commands + GUI), moved out of
// main.cpp so both UIs resolve profiles and captures identically.
#include <atomic>
#include <string>
#include <linux/input.h>
#include "controller/DeviceIdentity.h"
#include "controller/PhysicalController.h"

// Per-device profiles (devices/<key>.json): search cwd, exe dir, project root,
// then fall back to the project root as the write destination for new profiles.
std::string findDeviceProfilePath(const DeviceIdentity& id);

// 0..INTMAX or -1 when the argument is not a positive integer.
int parseId(const std::string& s);

// Capture with a short polling chunk so a stop request (Ctrl+C) is honoured
// while the caller waits for input. Returns None on timeout OR when stopRequested.
CaptureResult captureWithAbort(PhysicalController& controller, int timeoutMs,
                               const std::atomic<bool>& stopRequested);

// Rebuilds the bound PhysicalSource from a capture, including the hat direction
// (a hat capture's value is the raw -1/+1).
inline PhysicalSource sourceFromCapture(const CaptureResult& r) {
    if (r.kind == CaptureKind::Button) return buttonSource(r.code);
    if (isHatAxisCode(r.code)) return hatSource(r.code, (r.value > 0) ? 1 : -1);
    return axisSource(r.code);
}

// evdev event type used to name a source (`EV_KEY` for buttons, `EV_ABS` for hats/axes).
inline int sourceEventType(SourceKind kind) {
    return kind == SourceKind::Button ? EV_KEY : EV_ABS;
}