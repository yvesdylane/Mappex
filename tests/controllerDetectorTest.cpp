#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include "input/ControllerDetector.h"

// Creates a synthetic uinput device reporting the given name with a joystick-range
// button and optional stick axis, and returns its fd (or -1).
static int createSyntheticDevice(const char* name, bool withAxis) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) return -1;

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, BTN_TRIGGER);

    if (withAxis) {
        ioctl(fd, UI_SET_EVBIT, EV_ABS);
        struct uinput_abs_setup abs{};
        abs.code = ABS_X;
        abs.absinfo.minimum = -32768;
        abs.absinfo.maximum = 32767;
        ioctl(fd, UI_ABS_SETUP, &abs);
    }

    struct uinput_setup usetup{};
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = 0x810;
    usetup.id.product = 0x1;
    strncpy(usetup.name, name, sizeof(usetup.name) - 1);
    ioctl(fd, UI_DEV_SETUP, &usetup);
    ioctl(fd, UI_DEV_CREATE);

    return fd;
}

static bool controllerWithName(const std::string& name) {
    for (const auto& pad : ControllerDetector().findGamepads()) {
        if (pad.name == name) return true;
    }
    return false;
}

int main() {
    int gamepadFd = createSyntheticDevice("DetectorTestPad", true);
    if (gamepadFd < 0) {
        fprintf(stderr, "SKIP: cannot open /dev/uinput\n");
        return 0;
    }

    // Keyboard-like device: joystick-range button but no EV_ABS axes at all.
    // Must NOT be classified as a gamepad.
    int keyboardFd = createSyntheticDevice("DetectorTestKeyboard", false);
    if (keyboardFd < 0) {
        fprintf(stderr, "SKIP: cannot open /dev/uinput\n");
        ioctl(gamepadFd, UI_DEV_DESTROY);
        close(gamepadFd);
        return 0;
    }

    sleep(1);  // let the kernel register the new devices

    assert(controllerWithName("DetectorTestPad") && "synthetic gamepad was not detected");
    assert(!controllerWithName("DetectorTestKeyboard") && "keyboard without axes was detected as gamepad");

    // Single-path classification used by the hot-plug monitor.
    std::string padPath;
    for (const auto& pad : ControllerDetector().findGamepads()) {
        if (pad.name == "DetectorTestPad") padPath = pad.path;
    }
    assert(!padPath.empty() && "pad path not discovered");
    std::string padName;
    assert(ControllerDetector::isGamepadDevice(padPath, &padName) && "isGamepadDevice rejected a gamepad");
    assert(padName == "DetectorTestPad");
    assert(!ControllerDetector::isGamepadDevice("/dev/input/nonexistent0", nullptr) && "bogus path classified as gamepad");

    ioctl(gamepadFd, UI_DEV_DESTROY);
    ioctl(keyboardFd, UI_DEV_DESTROY);
    close(gamepadFd);
    close(keyboardFd);

    printf("controllerDetectorTest: all assertions passed\n");
    return 0;
}