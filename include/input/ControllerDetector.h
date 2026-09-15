#pragma once
#include <string>
#include <vector>

struct DetectedController {
    std::string path;
    std::string name;
};

class ControllerDetector {
public:
    std::vector<DetectedController> findGamepads() const;

    // Classifies one specific device path. Fills outName when it is a gamepad.
    static bool isGamepadDevice(const std::string& path, std::string* outName = nullptr);
};