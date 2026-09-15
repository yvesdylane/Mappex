#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <libevdev/libevdev.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s /dev/input/eventX\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    struct libevdev* dev = nullptr;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        fprintf(stderr, "Failed to init libevdev (%s)\n", strerror(-rc));
        return 1;
    }

    printf("Reading from: %s\n", libevdev_get_name(dev));
    printf("Press Ctrl+C to stop.\n\n");

    while (true) {
        struct input_event ev;
        rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);

        if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
            if (ev.type == EV_SYN) continue;  // skip sync markers for now

            printf("type=%-10s code=%-16s value=%d\n",
                   libevdev_event_type_get_name(ev.type),
                   libevdev_event_code_get_name(ev.type, ev.code),
                   ev.value);
        } else if (rc == -EAGAIN) {
            usleep(1000);  // nothing pending, avoid busy-spinning the CPU
        } else if (rc < 0) {
            fprintf(stderr, "Error reading event: %s\n", strerror(-rc));
            break;
        }
    }

    libevdev_free(dev);
    close(fd);
    return 0;
}