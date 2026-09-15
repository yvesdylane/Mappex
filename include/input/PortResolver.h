#pragma once
#include <string>

// Maps a /dev/input/eventN node back to the USB-port-topology string in
// /dev/input/by-path. Two identical pads without serials are unknowable except
// by which physical port they sit in — this is that tiebreaker.
class PortResolver {
public:
    // Returns the by-path symlink name pointing at devicePath ("pci-...:1:1.1-event-joystick"),
    // or "" when the device has no by-path link.
    static std::string resolvePathKey(const std::string& devicePath);
};