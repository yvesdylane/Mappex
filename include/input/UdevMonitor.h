#pragma once
// Listens to udev netlink for input-device add/remove events and forwards the
// /dev/input/eventN node. Callbacks may fire from the monitor thread.
#include <atomic>
#include <functional>
#include <string>
#include <thread>

using UdevEventHandler = std::function<void(const std::string& action, const std::string& devNode)>;

class UdevMonitor {
public:
    UdevMonitor() = default;
    ~UdevMonitor();

    UdevMonitor(const UdevMonitor&) = delete;
    UdevMonitor& operator=(const UdevMonitor&) = delete;

    void start(UdevEventHandler handler);
    void stop();

    bool started() const { return running_.load(); }

private:
    void runLoop(UdevEventHandler handler);

    std::atomic<bool> running_{false};
    std::thread monitorThread_;
};