#include "input/UdevMonitor.h"

#include <libudev.h>
#include <poll.h>
#include <unistd.h>
#include <cstring>
#include <string>

namespace {

constexpr char kSubsystem[] = "input";

bool isEventNode(const char* node) {
    return node && std::strncmp(node, "/dev/input/event", 16) == 0;
}

}  // namespace

UdevMonitor::~UdevMonitor() {
    stop();
}

void UdevMonitor::start(UdevEventHandler handler) {
    if (running_.exchange(true)) return;
    monitorThread_ = std::thread(&UdevMonitor::runLoop, this, std::move(handler));
}

void UdevMonitor::stop() {
    if (!running_.exchange(false)) return;
    if (monitorThread_.joinable()) monitorThread_.join();
}

void UdevMonitor::runLoop(UdevEventHandler handler) {
    struct udev* udev = udev_new();
    if (!udev) return;

    struct udev_monitor* mon = udev_monitor_new_from_netlink(udev, "udev");
    if (!mon) {
        udev_unref(udev);
        return;
    }
    udev_monitor_filter_add_match_subsystem_devtype(mon, kSubsystem, nullptr);
    udev_monitor_enable_receiving(mon);

    int fd = udev_monitor_get_fd(mon);
    std::string action;

    while (running_.load()) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        int rc = poll(&pfd, 1, 500);
        if (rc == 0) continue;           // timeout
        if (rc < 0) break;               // error
        if (!(pfd.revents & POLLIN)) continue;

        struct udev_device* dev = udev_monitor_receive_device(mon);
        if (!dev) continue;
        const char* node = udev_device_get_devnode(dev);
        if (isEventNode(node)) {
            action = udev_device_get_action(dev) ? udev_device_get_action(dev) : "";
            if (handler) handler(action, node);
        }
        udev_device_unref(dev);
    }

    udev_monitor_unref(mon);
    udev_unref(udev);
    running_.store(false);
}