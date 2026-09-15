#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <libevdev/libevdev.h>
#include <linux/uinput.h>

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

    // ---- open physical device ----
    int inFd = open(argv[1], O_RDONLY | O_NONBLOCK);
    if (inFd < 0) { perror("open input"); return 1; }

    struct libevdev* dev = nullptr;
    if (libevdev_new_from_fd(inFd, &dev) < 0) {
        fprintf(stderr, "libevdev init failed\n");
        return 1;
    }
    printf("Reading from: %s\n", libevdev_get_name(dev));

    // ---- create virtual device with the SAME key capabilities ----
    int outFd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (outFd < 0) { perror("open uinput"); return 1; }

    ioctl(outFd, UI_SET_EVBIT, EV_KEY);
    // Copy every EV_KEY code the real device supports onto the virtual one
    for (int code = 0; code < KEY_MAX; code++) {
        if (libevdev_has_event_code(dev, EV_KEY, code)) {
            ioctl(outFd, UI_SET_KEYBIT, code);
        }
    }

    struct uinput_setup usetup{};
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = 0x1234;
    usetup.id.product = 0x5679;
    strcpy(usetup.name, "Passthrough Virtual Pad");
    ioctl(outFd, UI_DEV_SETUP, &usetup);
    ioctl(outFd, UI_DEV_CREATE);
    sleep(1);

    printf("Virtual passthrough device created. Press buttons on your real controller (Ctrl+C to stop).\n");

    // ---- main loop: read real event, mirror to virtual ----
    while (true) {
        struct input_event ev;
        int rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

        if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
            if (ev.type == EV_KEY) {
                printf("Forwarding: code=%s value=%d\n",
                       libevdev_event_code_get_name(ev.type, ev.code), ev.value);
                emit(outFd, EV_KEY, ev.code, ev.value);
                emit(outFd, EV_SYN, SYN_REPORT, 0);
            }
            // EV_ABS (sticks) skipped for now — that's next once buttons are solid
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