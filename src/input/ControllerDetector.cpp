#include "input/ControllerDetector.h"
#include "Logger.h"
#include <libevdev/libevdev.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <dirent.h>

namespace {

bool isGamepad(struct libevdev* dev) {
    if (!libevdev_has_event_type(dev, EV_KEY)) return false;
    if (!libevdev_has_event_type(dev, EV_ABS)) return false;  // gamepads have axes; keyboards don't

    bool hasJoystickButton = false;
    for (int code = BTN_JOYSTICK; code <= BTN_THUMBR; ++code) {
        if (libevdev_has_event_code(dev, EV_KEY, code)) {
            hasJoystickButton = true;
            break;
        }
    }
    if (!hasJoystickButton) return false;

    // Require at least one actual stick axis, not just any EV_ABS bit
    return libevdev_has_event_code(dev, EV_ABS, ABS_X) ||
           libevdev_has_event_code(dev, EV_ABS, ABS_Y);
}

int eventIndex(const std::string& path) {
    // extract trailing digits from ".../eventN"
    std::string digits;
    for (auto it = path.rbegin(); it != path.rend() && std::isdigit(static_cast<unsigned char>(*it)); ++it) {
        digits.push_back(*it);
    }
    std::reverse(digits.begin(), digits.end());
    return digits.empty() ? -1 : std::stoi(digits);
}

}  // namespace

bool ControllerDetector::isGamepadDevice(const std::string& path, std::string* outName) {
    int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) return false;

    struct libevdev* dev = nullptr;
    bool result = false;
    if (libevdev_new_from_fd(fd, &dev) == 0) {
        result = isGamepad(dev);
        if (result && outName) *outName = libevdev_get_name(dev);
        libevdev_free(dev);
    }
    close(fd);
    return result;
}

std::vector<DetectedController> ControllerDetector::findGamepads() const {
    std::vector<DetectedController> result;

    DIR* dir = opendir("/dev/input");
    if (!dir) {
        Logger::error("opendir /dev/input: " + std::string(strerror(errno)));
        return result;
    }

    while (struct dirent* entry = readdir(dir)) {
        std::string name = entry->d_name;
        if (name.rfind("event", 0) != 0) continue;

        std::string path = "/dev/input/" + name;

        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        struct libevdev* dev = nullptr;
        if (libevdev_new_from_fd(fd, &dev) == 0) {
            if (isGamepad(dev)) {
                result.push_back({ path, libevdev_get_name(dev) });
            }
            libevdev_free(dev);
        }
        close(fd);
    }

    closedir(dir);

    std::sort(result.begin(), result.end(),
              [](const DetectedController& a, const DetectedController& b) {
                  return eventIndex(a.path) < eventIndex(b.path);
              });

    return result;
}