// ControllerManager: thread-safe per-controller ownership of reader + output.
// Uses real synthetic uinput gamepads. Returns 0 on success, 1 on failure.
#include "controller/ControllerManager.h"
#include "controller/DeviceProfile.h"
#include "controller/PhysicalController.h"
#include "syntheticPad.h"

#include <cassert>
#include <cstdio>
#include <vector>

int main() {
    ControllerManager mgr;

    // Unknown device -> no id, nothing attached.
    assert(mgr.attach("/dev/input/nonexistent99", nullptr) == -1);
    assert(mgr.ids().empty());
    assert(mgr.get(0) == nullptr);
    assert(mgr.getManaged(0) == nullptr);
    assert(mgr.findByPath("/dev/input/nonexistent99") == -1);

    testpad::TestPad padA = testpad::TestPad::create("ManagerPadA");
    if (padA.fd < 0) {
        fprintf(stderr, "SKIP: /dev/uinput not available\n");
        return 2;
    }
    std::string aPath = padA.findPath();
    assert(!aPath.empty() && "pad A never got a /dev/input/eventN node");

    int idA = mgr.attach(aPath, nullptr);
    assert(idA == 0 && "first attach should assign id 0");
    assert(mgr.findByPath(aPath) == 0);
    assert(mgr.get(0) != nullptr && "get(0) after attach");
    assert(mgr.getManaged(0) != nullptr);
    assert(mgr.getManaged(0)->deviceName == "ManagerPadA");
    assert(mgr.getManaged(0)->profile != nullptr && "attach builds a device profile");
    assert(mgr.getManaged(0)->profile->identity.vendorId != 0 && "profile captured the pad identity");
    {
        std::vector<int> all = mgr.ids();
        assert(all.size() == 1 && all[0] == 0);
    }

    // Second pad gets an independent id.
    testpad::TestPad padB = testpad::TestPad::create("ManagerPadB");
    if (padB.fd < 0) {
        fprintf(stderr, "SKIP: /dev/uinput not available\n");
        return 2;
    }
    std::string bPath = padB.findPath();
    assert(!bPath.empty() && "pad B never got a /dev/input/eventN node");

    int idB = mgr.attach(bPath, nullptr);
    assert(idB == 1 && "second attach should assign id 1");
    assert(mgr.getManaged(1)->deviceName == "ManagerPadB");
    assert(mgr.findByPath(bPath) == 1);
    assert(!mgr.getManaged(1)->controller->isActive() && "autoMap off => inactive");
    {
        std::vector<int> all = mgr.ids();
        assert(all.size() == 2 && all[0] == 0 && all[1] == 1);
    }

    // Passing a profile with autoMap on must flip the controller into the active
    // (grabbed) state right after attach.
    {
        auto profile = std::make_shared<DeviceProfile>();
        profile->identity = mgr.getManaged(1)->profile->identity;
        profile->virtualIdentity = mgr.getManaged(1)->profile->virtualIdentity;
        profile->autoMap = true;
        int id = mgr.attach(bPath, profile);
        assert(id == 2);
        assert(mgr.getManaged(2)->controller->isActive() && "autoMap on => active after attach");
    }
    mgr.detach(2);
    mgr.detach(1);

    // Our own virtual outputs must never re-attach as input. The virtual device
    // now spoofs a real Xbox 360 vendor id, so the guard is path-based: each
    // attach records the /dev/input/eventN node it created.
    {
        auto virtualPaths = mgr.knownVirtualPaths();
        assert(!virtualPaths.empty() && "attach() must record its virtual output node");
        for (const auto& vp : virtualPaths) {
            assert(!vp.empty());
            assert(mgr.isKnownVirtualPath(vp));
            assert(mgr.attach(vp, nullptr) == -1 && "own virtual node must be refused");
        }
    }

    // Detach A: entry gone, nothing left.
    mgr.detach(0);
    assert(mgr.findByPath(aPath) == -1);
    assert(mgr.get(0) == nullptr);
    {
        std::vector<int> all = mgr.ids();
        assert(all.empty());
    }

    // Unknown detach is a safe no-op.
    mgr.detach(999 + 1);

    // detachAll on an empty manager is safe; ids stay empty.
    mgr.detachAll();
    assert(mgr.ids().empty());

    printf("controllerManagerTest: all assertions passed\n");
    return 0;
}