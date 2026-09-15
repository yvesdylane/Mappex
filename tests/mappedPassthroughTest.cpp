#include <cstdio>
#include <cstring>
#include <cerrno>
#include <unordered_map>
#include <fcntl.h>
#include <unistd.h>
#include <libevdev/libevdev.h>
#include <linux/uinput.h>

// ---- the internal vocabulary your whole app will eventually speak ----
enum class LogicalControl {
    A, B, X, Y, LB, RB, Unmapped
};

// ---- Physical -> Logical: THIS is what your JSON mapping will replace later ----
static const std::unordered_map<int, LogicalControl> physicalToLogical = {
    { BTN_TRIGGER, LogicalControl::A },
    { BTN_THUMB,   LogicalControl::B },
    { BTN_THUMB2,  LogicalControl::X },
    { BTN_TOP,     LogicalControl::Y },
    { BTN_TOP2,    LogicalControl::LB },
    { BTN_PINKIE,  LogicalControl::RB },
};

// ---- Logical -> Virtual Xbox event code: this side stays fixed ----
static const std::unordered_map<LogicalControl, int> logicalToVirtual = {
    { LogicalControl::A,  BTN_SOUTH },
    { LogicalControl::B,  BTN_EAST },
    { LogicalControl::X,  BTN_WEST },
    { LogicalControl::Y,  BTN_NORTH },
    { LogicalControl::LB, BTN_TL },
    { LogicalControl::RB, BTN_TR },
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

static void emit(int fd, int type, int code, int val) {
    struct input_event ev{};
    ev.type = type;
    ev.code = code;
    ev.value = val;
    write(fd, &ev, sizeof(ev));
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s /dev/input/eventX\n", argv[0]);
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

    // ---- virtual device now only exposes real Xbox-style codes ----
    int outFd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (outFd < 0) { perror("open uinput"); return 1; }

    ioctl(outFd, UI_SET_EVBIT, EV_KEY);
    for (auto& [logical, virtCode] : logicalToVirtual) {
        ioctl(outFd, UI_SET_KEYBIT, virtCode);
    }

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