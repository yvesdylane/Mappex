// UdevMonitor: fires add/remove callbacks for real udev events. Uses a real
// synthetic uinput pad. Returns 0 on success, 1 on failure, 2 if skipped.
#include "input/UdevMonitor.h"
#include "syntheticPad.h"

#include <cassert>
#include <cstdio>
#include <mutex>
#include <string>
#include <unistd.h>

namespace {

struct SeenEvents {
    std::mutex m;
    std::string added;
    std::string removed;
};

std::string pollUntil(SeenEvents& events, std::string SeenEvents::*field, int maxTriesMs) {
    for (int elapsed = 0; elapsed < maxTriesMs; elapsed += 200) {
        {
            std::lock_guard<std::mutex> lock(events.m);
            if (!(events.*field).empty()) return events.*field;
        }
        usleep(200000);
    }
    return "";
}

}  // namespace

int main() {
    SeenEvents events;

    UdevMonitor monitor;
    monitor.start([&events](const std::string& action, const std::string& node) {
        std::lock_guard<std::mutex> lock(events.m);
        if (action == "add") events.added = node;
        else if (action == "remove") events.removed = node;
    });
    assert(monitor.started() && "monitor did not start");

    testpad::TestPad pad = testpad::TestPad::create("UdevTestPad");
    if (pad.fd < 0) {
        fprintf(stderr, "SKIP: /dev/uinput not available\n");
        monitor.stop();
        return 2;
    }

    std::string addedNode = pollUntil(events, &SeenEvents::added, 6000);
    assert(!addedNode.empty() && "no add event for new uinput pad");
    assert(addedNode.rfind("/dev/input/event", 0) == 0 && "add event was not an /dev/input/eventN node");

    pad.destroy();

    std::string removedNode = pollUntil(events, &SeenEvents::removed, 6000);
    assert(!removedNode.empty() && "no remove event for destroyed pad");

    monitor.stop();
    printf("udevMonitorTest: all assertions passed\n");
    return 0;
}