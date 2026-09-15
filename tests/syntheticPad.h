#pragma once
// Test-only helper: synthetic uinput gamepad for detector/manager/udev tests.
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <dirent.h>
#include <linux/input.h>
#include <linux/uinput.h>

namespace testpad {

struct TestPad {
    int fd = -1;
    std::string name;

    static TestPad create(const std::string& name, bool withAxis = true,
                          uint32_t vendor = 0x810, uint32_t product = 0x1) {
        TestPad p;
        p.name = name;
        p.fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        if (p.fd < 0) return p;

        ioctl(p.fd, UI_SET_EVBIT, EV_KEY);
        ioctl(p.fd, UI_SET_KEYBIT, BTN_TRIGGER);

        if (withAxis) {
            ioctl(p.fd, UI_SET_EVBIT, EV_ABS);
            struct uinput_abs_setup abs{};
            abs.code = ABS_X;
            abs.absinfo.minimum = -32768;
            abs.absinfo.maximum = 32767;
            ioctl(p.fd, UI_ABS_SETUP, &abs);
        }

        struct uinput_setup usetup{};
        usetup.id.bustype = BUS_USB;
        usetup.id.vendor  = vendor;
        usetup.id.product = product;
        strncpy(usetup.name, name.c_str(), sizeof(usetup.name) - 1);
        ioctl(p.fd, UI_DEV_SETUP, &usetup);
        ioctl(p.fd, UI_DEV_CREATE);
        return p;
    }

    void destroy() {
        if (fd >= 0) {
            ioctl(fd, UI_DEV_DESTROY);
            close(fd);
            fd = -1;
        }
    }

    ~TestPad() { destroy(); }

    // Polls /sys for an eventN node whose device name matches. Empty string if none.
    std::string findPath(int maxTries = 40) const {
        for (int i = 0; i < maxTries; ++i) {
            usleep(300000);
            DIR* dir = opendir("/dev/input");
            if (!dir) continue;
            std::string found;
            while (struct dirent* e = readdir(dir)) {
                std::string dn = e->d_name;
                if (dn.rfind("event", 0) != 0) continue;
                std::string node = "/sys/class/input/" + dn + "/device/name";
                FILE* f = fopen(node.c_str(), "r");
                if (!f) continue;
                char buf[256] = {0};
                if (fgets(buf, sizeof(buf), f)) {
                    std::string n(buf);
                    while (!n.empty() && (n.back() == '\n' || n.back() == '\r')) n.pop_back();
                    if (n == name) found = "/dev/input/" + dn;
                }
                fclose(f);
                if (!found.empty()) break;
            }
            closedir(dir);
            if (!found.empty()) return found;
        }
        return "";
    }
};

}  // namespace testpad