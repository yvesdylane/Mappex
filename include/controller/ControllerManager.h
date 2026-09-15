#pragma once
// Owns one reader thread + virtual output device + active mapping per connected
// controller. Thread-safe: attach/detach may be called from the udev hot-plug
// thread while the terminal thread walks a mapping.
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class PhysicalController;
class DeviceProfile;

struct ManagedController {
    int id = -1;
    std::string devicePath;
    std::string deviceName;
    std::shared_ptr<DeviceProfile> profile;   // never null while attached
    std::unique_ptr<PhysicalController> controller;
    std::thread runnerThread;
};

class ControllerManager {
public:
    ControllerManager();
    ~ControllerManager();

    ControllerManager(const ControllerManager&) = delete;
    ControllerManager& operator=(const ControllerManager&) = delete;

    // Opens the device, creates its own virtual output and reader thread, and
    // applies the given profile (identity + mapping + autoMap). Returns the
    // assigned id, or -1 on failure. Paths of our own previously-created virtual
    // devices are refused so our outputs never re-attach as input.
    int attach(const std::string& devicePath, std::shared_ptr<DeviceProfile> profile);

    // Removes the entry under the lock, then stops+joins its thread outside it.
    // Safe for unknown ids.
    void detach(int id);

    void detachAll();

    // Raw controller for interactive mapping walks. Nullptr if id unknown.
    PhysicalController* get(int id);

    // Full managed entry. nullptr if id unknown.
    ManagedController* getManaged(int id);

    // Id of the controller on the given device path, or -1.
    int findByPath(const std::string& devicePath) const;

    // True when the path is one of the virtual devices we created ourselves.
    // Udev "add" events for these must never become physical controllers.
    bool isKnownVirtualPath(const std::string& devicePath) const;

    // Every /dev/input/eventN node we have created as a virtual output device.
    std::set<std::string> knownVirtualPaths() const;

    std::vector<int> ids() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<int, std::unique_ptr<ManagedController>> byId_;
    std::set<std::string> knownVirtualPaths_;
    int nextId_ = 0;
};