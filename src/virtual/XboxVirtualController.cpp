#include "virtual/XboxVirtualController.h"
#include <linux/input.h>

XboxVirtualController::XboxVirtualController(std::unique_ptr<OutputBackend> b)
    : backend(std::move(b)) {}

int XboxVirtualController::buttonCode(LogicalControl control) {
    switch (control) {
        case LogicalControl::A:        return BTN_SOUTH;
        case LogicalControl::B:        return BTN_EAST;
        case LogicalControl::X:        return BTN_WEST;
        case LogicalControl::Y:        return BTN_NORTH;
        case LogicalControl::LB:       return BTN_TL;
        case LogicalControl::RB:       return BTN_TR;
        case LogicalControl::Start:    return BTN_START;
        case LogicalControl::Back:     return BTN_SELECT;
        case LogicalControl::Home:     return BTN_MODE;
        case LogicalControl::LS:       return BTN_THUMBL;
        case LogicalControl::RS:       return BTN_THUMBR;
        case LogicalControl::DPadUp:    return BTN_DPAD_UP;
        case LogicalControl::DPadDown:  return BTN_DPAD_DOWN;
        case LogicalControl::DPadLeft:  return BTN_DPAD_LEFT;
        case LogicalControl::DPadRight: return BTN_DPAD_RIGHT;
        default:                       return -1;
    }
}

int XboxVirtualController::axisCode(LogicalAxis axis) {
    switch (axis) {
        case LogicalAxis::LeftStickX:  return ABS_X;
        case LogicalAxis::LeftStickY:  return ABS_Y;
        case LogicalAxis::RightStickX: return ABS_RX;
        case LogicalAxis::RightStickY: return ABS_RY;
        case LogicalAxis::LeftTrigger: return ABS_Z;
        case LogicalAxis::RightTrigger: return ABS_RZ;
        default:                       return -1;
    }
}

void XboxVirtualController::registerCapabilities() {
    for (auto control : { LogicalControl::A, LogicalControl::B, LogicalControl::X,
                          LogicalControl::Y, LogicalControl::LB, LogicalControl::RB,
                          LogicalControl::Start, LogicalControl::Back, LogicalControl::Home,
                          LogicalControl::LS, LogicalControl::RS,
                          LogicalControl::DPadUp, LogicalControl::DPadDown,
                          LogicalControl::DPadLeft, LogicalControl::DPadRight }) {
        int code = buttonCode(control);
        if (code >= 0) backend->addButtonCapability(code);
    }
    for (auto axis : { LogicalAxis::LeftStickX, LogicalAxis::LeftStickY,
                       LogicalAxis::RightStickX, LogicalAxis::RightStickY }) {
        int code = axisCode(axis);
        if (code >= 0) backend->addAxisCapability(code, -32768, 32767);
    }
    for (auto axis : { LogicalAxis::LeftTrigger, LogicalAxis::RightTrigger }) {
        int code = axisCode(axis);
        if (code >= 0) backend->addAxisCapability(code, 0, 255);
    }
}

bool XboxVirtualController::create() {
    registerCapabilities();
    return backend->create();
}

bool XboxVirtualController::create(const VirtualDeviceInfo& info) {
    registerCapabilities();
    return backend->create(info);
}

void XboxVirtualController::reset() {
    backend->reset();
}

std::string XboxVirtualController::outputDevicePath() const {
    return backend->devicePath();
}

int XboxVirtualController::pressButton(LogicalControl control, int value) {
    int code = buttonCode(control);
    if (code < 0) return -1;
    backend->sendButton(code, value);
    return code;
}

void XboxVirtualController::setAxis(LogicalAxis axis, int value) {
    int code = axisCode(axis);
    if (code < 0) return;
    backend->sendAxis(code, value);
}
