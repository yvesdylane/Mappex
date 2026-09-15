#pragma once
#include <string>
#include <vector>
#include <utility>
#include <linux/input.h>

// True for the multi-directional hat axes (D-pads, mouse wheels...). These are
// exposed as EV_ABS with -1/0/+1 values, not as EV_KEY buttons.
inline bool isHatAxisCode(int code) {
    return code >= ABS_HAT0X && code <= ABS_HAT3Y;
}

enum class LogicalControl {
    A, B, X, Y,
    LB, RB,
    Start, Back, Home,
    LS, RS,                                    // stick-click buttons
    DPadUp, DPadDown, DPadLeft, DPadRight,     // D-pad: conventional digital buttons
    Unmapped
};

enum class LogicalAxis {
    LeftStickX, LeftStickY, RightStickX, RightStickY,
    LeftTrigger, RightTrigger,
    Invalid
};

inline const char* logicalControlName(LogicalControl c) {
    switch (c) {
        case LogicalControl::A: return "A";
        case LogicalControl::B: return "B";
        case LogicalControl::X: return "X";
        case LogicalControl::Y: return "Y";
        case LogicalControl::LB: return "LB";
        case LogicalControl::RB: return "RB";
        case LogicalControl::Start: return "Start";
        case LogicalControl::Back: return "Back";
        case LogicalControl::Home: return "Home";
        case LogicalControl::LS: return "LS";
        case LogicalControl::RS: return "RS";
        case LogicalControl::DPadUp: return "DPadUp";
        case LogicalControl::DPadDown: return "DPadDown";
        case LogicalControl::DPadLeft: return "DPadLeft";
        case LogicalControl::DPadRight: return "DPadRight";
        default: return "Unmapped";
    }
}

inline const char* logicalAxisName(LogicalAxis a) {
    switch (a) {
        case LogicalAxis::LeftStickX: return "LeftStickX";
        case LogicalAxis::LeftStickY: return "LeftStickY";
        case LogicalAxis::RightStickX: return "RightStickX";
        case LogicalAxis::RightStickY: return "RightStickY";
        case LogicalAxis::LeftTrigger: return "LeftTrigger";
        case LogicalAxis::RightTrigger: return "RightTrigger";
        default: return "Invalid";
    }
}

inline LogicalControl logicalControlFromName(const std::string& n) {
    if (n == "A") return LogicalControl::A;
    if (n == "B") return LogicalControl::B;
    if (n == "X") return LogicalControl::X;
    if (n == "Y") return LogicalControl::Y;
    if (n == "LB") return LogicalControl::LB;
    if (n == "RB") return LogicalControl::RB;
    if (n == "Start") return LogicalControl::Start;
    if (n == "Back") return LogicalControl::Back;
    if (n == "Home") return LogicalControl::Home;
    if (n == "LS") return LogicalControl::LS;
    if (n == "RS") return LogicalControl::RS;
    if (n == "DPadUp") return LogicalControl::DPadUp;
    if (n == "DPadDown") return LogicalControl::DPadDown;
    if (n == "DPadLeft") return LogicalControl::DPadLeft;
    if (n == "DPadRight") return LogicalControl::DPadRight;
    return LogicalControl::Unmapped;
}

inline LogicalAxis logicalAxisFromName(const std::string& n) {
    if (n == "LeftStickX") return LogicalAxis::LeftStickX;
    if (n == "LeftStickY") return LogicalAxis::LeftStickY;
    if (n == "RightStickX") return LogicalAxis::RightStickX;
    if (n == "RightStickY") return LogicalAxis::RightStickY;
    if (n == "LeftTrigger") return LogicalAxis::LeftTrigger;
    if (n == "RightTrigger") return LogicalAxis::RightTrigger;
    return LogicalAxis::Invalid;
}

// Full ordered walk used by guided mapping — buttons, then triggers, then sticks,
// then the four D-pad directions (each physical hat direction binds one button).
inline std::vector<std::pair<std::string, LogicalControl>> guidedButtonList() {
    return {
        {"A", LogicalControl::A}, {"B", LogicalControl::B},
        {"X", LogicalControl::X}, {"Y", LogicalControl::Y},
        {"LB", LogicalControl::LB}, {"RB", LogicalControl::RB},
        {"Start", LogicalControl::Start}, {"Back", LogicalControl::Back},
        {"Home", LogicalControl::Home},
        {"LS", LogicalControl::LS}, {"RS", LogicalControl::RS},
    };
}

inline std::vector<std::pair<std::string, LogicalAxis>> guidedTriggerList() {
    return { {"LeftTrigger", LogicalAxis::LeftTrigger}, {"RightTrigger", LogicalAxis::RightTrigger} };
}

inline std::vector<std::pair<std::string, LogicalAxis>> guidedStickList() {
    return {
        {"LeftStickX", LogicalAxis::LeftStickX}, {"LeftStickY", LogicalAxis::LeftStickY},
        {"RightStickX", LogicalAxis::RightStickX}, {"RightStickY", LogicalAxis::RightStickY},
    };
}

inline std::vector<std::pair<std::string, LogicalControl>> guidedDPadList() {
    return {
        {"DPadLeft", LogicalControl::DPadLeft}, {"DPadRight", LogicalControl::DPadRight},
        {"DPadUp", LogicalControl::DPadUp}, {"DPadDown", LogicalControl::DPadDown},
    };
}
