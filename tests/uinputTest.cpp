#include <cstdio>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <linux/uinput.h>

static void emit(int fd, int type, int code, int val) {
    struct input_event ev{};
    ev.type = type;
    ev.code = code;
    ev.value = val;
    write(fd, &ev, sizeof(ev));
}

int main() {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open /dev/uinput");
        return 1;
    }

    // Tell the kernel this device can send key/button events
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, BTN_SOUTH);

    struct uinput_setup usetup{};
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = 0x1234;
    usetup.id.product = 0x5678;
    strcpy(usetup.name, "My Test Virtual Pad");

    ioctl(fd, UI_DEV_SETUP, &usetup);
    ioctl(fd, UI_DEV_CREATE);

    // Give userspace (and this program) a moment to see the new device
    sleep(1);

    printf("Virtual device created. Sending BTN_SOUTH press...\n");

    emit(fd, EV_KEY, BTN_SOUTH, 1);              // press
    emit(fd, EV_SYN, SYN_REPORT, 0);
    sleep(1);
    emit(fd, EV_KEY, BTN_SOUTH, 0);              // release
    emit(fd, EV_SYN, SYN_REPORT, 0);

    printf("Done. Device stays alive for 5 seconds so you can check it with evtest.\n");
    sleep(5);

    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    return 0;
}