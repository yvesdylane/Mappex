#include "virtual/UInputBackend.h"
#include "Logger.h"
#include <linux/uinput.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cctype>
#include <cerrno>
#include <cstring>

void UInputBackend::addButtonCapability(int code) { buttonCodes.push_back(code); }

void UInputBackend::addAxisCapability(int code, int minValue, int maxValue) {
    axisCodes.push_back({ code, minValue, maxValue });
}

static void emitEvent(int fd, int type, int code, int val) {
    struct input_event ev{};
    ev.type = type;
    ev.code = code;
    ev.value = val;
    write(fd, &ev, sizeof(ev));
}

bool UInputBackend::create() {
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        Logger::error("open uinput: " + std::string(strerror(errno)));
        return false;
    }

    if (!buttonCodes.empty()) {
        ioctl(fd, UI_SET_EVBIT, EV_KEY);
        for (int code : buttonCodes) ioctl(fd, UI_SET_KEYBIT, code);
    }

    if (!axisCodes.empty()) {
        ioctl(fd, UI_SET_EVBIT, EV_ABS);
        for (const auto& axis : axisCodes) {
            ioctl(fd, UI_SET_ABSBIT, axis.code);
            struct uinput_abs_setup absSetup{};
            absSetup.code = axis.code;
            absSetup.absinfo.minimum = axis.minValue;
            absSetup.absinfo.maximum = axis.maxValue;
            absSetup.absinfo.flat = (axis.maxValue - axis.minValue) / 32;   // ~3% deadzone, scales with range
            absSetup.absinfo.fuzz = 16;
            ioctl(fd, UI_ABS_SETUP, &absSetup);
        }
    }

    struct uinput_setup usetup{};
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = vendorId_;
    usetup.id.product = productId_;
    usetup.id.version = version_;
    strncpy(usetup.name, deviceName_.c_str(), UINPUT_MAX_NAME_SIZE - 1);
    ioctl(fd, UI_DEV_SETUP, &usetup);
    ioctl(fd, UI_DEV_CREATE);
    sleep(1);
    return true;
}

bool UInputBackend::create(const VirtualDeviceInfo& info) {
    deviceName_ = info.name;
    vendorId_ = info.vendorId;
    productId_ = info.productId;
    version_ = info.version;
    return create();
}

void UInputBackend::reset() {
    if (fd < 0) return;
    for (int code : buttonCodes) sendButton(code, 0);
    for (const auto& axis : axisCodes) sendAxis(axis.code, (axis.minValue + axis.maxValue) / 2);
}

std::string UInputBackend::devicePath() const {
    if (fd < 0) return {};
    char sysname[UINPUT_MAX_NAME_SIZE] = {0};
    if (ioctl(fd, UI_GET_SYSNAME(sizeof(sysname)), sysname) < 0) return {};

    // sysname is the input core's node name, e.g. "input15". The evdev event node
    // lives in sysfs as /sys/class/input/<sysname>/eventN -> /dev/input/eventN.
    std::string base = std::string("/sys/class/input/") + sysname;
    DIR* dir = opendir(base.c_str());
    if (!dir) return {};
    std::string result;
    while (struct dirent* ent = readdir(dir)) {
        if (std::strncmp(ent->d_name, "event", 5) == 0 &&
            std::isdigit(static_cast<unsigned char>(ent->d_name[5]))) {
            result = std::string("/dev/input/") + ent->d_name;
            break;
        }
    }
    closedir(dir);
    return result;
}

void UInputBackend::sendButton(int code, int value) {
    emitEvent(fd, EV_KEY, code, value);
    emitEvent(fd, EV_SYN, SYN_REPORT, 0);
}

void UInputBackend::sendAxis(int code, int value) {
    emitEvent(fd, EV_ABS, code, value);
    emitEvent(fd, EV_SYN, SYN_REPORT, 0);
}

UInputBackend::~UInputBackend() {
    if (fd >= 0) {
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
    }
}