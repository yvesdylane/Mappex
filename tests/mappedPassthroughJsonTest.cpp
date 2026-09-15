#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <unordered_map>
#include <fcntl.h>
#include <unistd.h>
#include <libevdev/libevdev.h>
#include <linux/uinput.h>

using json = nlohmann::json;

enum class LogicalControl {
    A, B, X, Y, LB, RB, Unmapped
};

static const char* logicalName(LogicalControl c) {
    switch (c) {
        case LogicalControl::A: return "A";
        case LogicalControl::B: return "B";
        case LogicalControl::X: return "X";
        case LogicalControl::Y: return "Y";
        case LogicalControl::LB: return "LB";
        case LogicalControl::RB: return "RB";
        default: return "Unmapped";
    }
}

// Logical -> Virtual Xbox event code: fixed, not user-editable
static const std::unordered_map<LogicalControl, int> logicalToVirtual = {
    { LogicalControl::A,  BTN_SOUTH },
    { LogicalControl::B,  BTN_EAST },
    { LogicalControl::X,  BTN_WEST },
    { LogicalControl::Y,  BTN_NORTH },
    { LogicalControl::LB, BTN_TL },
    { LogicalControl::RB, BTN_TR },
};

// Name <-> code lookup tables, used only while parsing the JSON
static const std::unordered_map<std::string, int> nameToCode = {
    { "BTN_TRIGGER", BTN_TRIGGER },
    { "BTN_THUMB",   BTN_THUMB },
    { "BTN_THUMB2",  BTN_THUMB2 },
    { "BTN_TOP",     BTN_TOP },
    { "BTN_TOP2",    BTN_TOP2 },
    { "BTN_PINKIE",  BTN_PINKIE },
    { "BTN_BASE",    BTN_BASE },
    { "BTN_BASE2",   BTN_BASE2 },
};

static const std::unordered_map<std::string, LogicalControl> nameToLogical = {
    { "A", LogicalControl::A },
    { "B", LogicalControl::B },
    { "X", LogicalControl::X },
    { "Y", LogicalControl::Y },
    { "LB", LogicalControl::LB },
    { "RB", LogicalControl::RB },
};

static void emit(int fd, int type, int code, int val) {
    struct input_event ev{};
    ev.type = type;
    ev.code = code;
    ev.value = val;
    write(fd, &ev, sizeof(ev));
}

static std::unordered_map<int, LogicalControl> loadMapping(const std::string& path) {
    std::unordered_map<int, LogicalControl> result;

    std::ifstream file(path);
    if (!file.is_open()) {
        fprintf(stderr, "Could not open mapping file: %s\n", path.c_str());
        return result;
    }

    json j;
    file >> j;

    for (auto& [physicalName, logicalNameVal] : j["mapping"].items()) {
        auto codeIt = nameToCode.find(physicalName);
        auto logicalIt = nameToLogical.find(logicalNameVal.get<std::string>());

        if (codeIt == nameToCode.end()) {
            fprintf(stderr, "Unknown physical button name in JSON: %s\n", physicalName.c_str());
            continue;
        }
        if (logicalIt == nameToLogical.end()) {
            fprintf(stderr, "Unknown logical control name in JSON: %s\n", logicalNameVal.get<std::string>().c_str());
            continue;
        }

        result[codeIt->second] = logicalIt->second;
    }

    printf("Loaded %zu mapping(s) from %s\n", result.size(), path.c_str());
    return result;
}

// Normalizes a raw axis value (e.g. 0-255) to -1.0 .. +1.0
static float normalizeAxis(struct libevdev* dev, int code, int rawValue) {
    const struct input_absinfo* info = libevdev_get_abs_info(dev, code);
    if (!info) return 0.0f;

    int min = info->minimum;
    int max = info->maximum;
    int mid = (min + max) / 2;
    int range = (max - min) / 2;

    if (range == 0) return 0.0f;

    float normalized = static_cast<float>(rawValue - mid) / static_cast<float>(range);

    // Apply a small deadzone so resting sticks don't jitter around 0
    const float deadzone = 0.05f;
    if (normalized > -deadzone && normalized < deadzone) return 0.0f;

    return normalized;
}

static void setupVirtualAxis(int fd, int code) {
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_ABSBIT, code);

    struct uinput_abs_setup absSetup{};
    absSetup.code = code;
    absSetup.absinfo.minimum = -32768;
    absSetup.absinfo.maximum = 32767;
    absSetup.absinfo.flat = 1000;   // small deadzone built into the virtual device itself
    absSetup.absinfo.fuzz = 16;

    ioctl(fd, UI_ABS_SETUP, &absSetup);
}

static const std::unordered_map<int, int> physicalToVirtualAxis = {
    { ABS_X,  ABS_X },   // your pad's left stick X -> virtual left stick X
    { ABS_Y,  ABS_Y },   // your pad's left stick Y -> virtual left stick Y
    { ABS_Z,  ABS_RX },  // your pad's "second stick" X -> virtual right stick X
    { ABS_RZ, ABS_RY },  // your pad's "second stick" Y -> virtual right stick Y
};

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s /dev/input/eventX\n", argv[0]);
        return 1;
    }

    // Load the mapping ONCE, before the loop starts
    auto physicalToLogical = loadMapping("mapping.json");
    if (physicalToLogical.empty()) {
        fprintf(stderr, "No mappings loaded, exiting.\n");
        return 1;
    }

    int inFd = open(argv[1], O_RDONLY | O_NONBLOCK);
    if (inFd < 0) { perror("open input"); return 1; }

    struct libevdev* dev = nullptr;
    if (libevdev_new_from_fd(inFd, &dev) < 0) {
        fprintf(stderr, "libevdev init failed\n");
        return 1;
    }
    printf("Reading from: %s\n", libevdev_get_name(dev));

    int outFd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (outFd < 0) { perror("open uinput"); return 1; }

    ioctl(outFd, UI_SET_EVBIT, EV_KEY);
    for (auto& [logical, virtCode] : logicalToVirtual) {
        ioctl(outFd, UI_SET_KEYBIT, virtCode);
    }

    setupVirtualAxis(outFd, ABS_X);   // left stick X
    setupVirtualAxis(outFd, ABS_Y);   // left stick Y
    setupVirtualAxis(outFd, ABS_RX);  // right stick X
    setupVirtualAxis(outFd, ABS_RY);  // right stick Y

    struct uinput_setup usetup{};
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = 0x1234;
    usetup.id.product = 0x567A;
    strcpy(usetup.name, "Mapped Virtual Xbox Pad");
    ioctl(outFd, UI_DEV_SETUP, &usetup);
    ioctl(outFd, UI_DEV_CREATE);
    sleep(1);

    printf("Virtual Xbox-style device created. Press buttons (Ctrl+C to stop).\n");

    while (true) {
        struct input_event ev;
        int rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

        if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
            if (ev.type == EV_KEY) {
                auto it = physicalToLogical.find(ev.code);
                if (it == physicalToLogical.end()) {
                    printf("Ignoring unmapped physical button: %s\n",
                           libevdev_event_code_get_name(ev.type, ev.code));
                    continue;
                }

                LogicalControl logical = it->second;
                int virtCode = logicalToVirtual.at(logical);

                printf("Physical %s -> Logical %s -> Virtual %s (value=%d)\n",
                       libevdev_event_code_get_name(ev.type, ev.code),
                       logicalName(logical),
                       libevdev_event_code_get_name(EV_KEY, virtCode),
                       ev.value);

                emit(outFd, EV_KEY, virtCode, ev.value);
                emit(outFd, EV_SYN, SYN_REPORT, 0);
            } else if (ev.type == EV_ABS) {
                auto axisIt = physicalToVirtualAxis.find(ev.code);
                if (axisIt == physicalToVirtualAxis.end()) {
                    continue;  // e.g. the hat/d-pad - not handled yet
                }

                float norm = normalizeAxis(dev, ev.code, ev.value);
                if (norm > 1.0f) norm = 1.0f;      // clamp the rounding overshoot
                if (norm < -1.0f) norm = -1.0f;

                int virtValue = static_cast<int>(norm * 32767.0f);

                printf("Axis %s -> virtual %s: %d\n",
                       libevdev_event_code_get_name(EV_ABS, ev.code),
                       libevdev_event_code_get_name(EV_ABS, axisIt->second),
                       virtValue);

                emit(outFd, EV_ABS, axisIt->second, virtValue);
                emit(outFd, EV_SYN, SYN_REPORT, 0);
            }
        } else if (rc == -EAGAIN) {
            usleep(1000);
        } else if (rc < 0) {
            fprintf(stderr, "Read error: %s\n", strerror(-rc));
            break;
        }
    }

    ioctl(outFd, UI_DEV_DESTROY);
    close(outFd);
    libevdev_free(dev);
    close(inFd);
    return 0;
}