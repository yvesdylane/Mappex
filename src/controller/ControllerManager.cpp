#include "controller/ControllerManager.h"
#include "controller/DeviceIdentity.h"
#include "controller/DeviceProfile.h"
#include "controller/Mapping.h"
#include "controller/PhysicalController.h"
#include "input/EvdevEventReader.h"
#include "input/PortResolver.h"
#include "utils/AppUtil.h"
#include "virtual/UInputBackend.h"
#include "virtual/XboxVirtualController.h"
#include "Logger.h"

#include <algorithm>
#include <unistd.h>

ControllerManager::ControllerManager() = default;

ControllerManager::~ControllerManager() {
    detachAll();
}

int ControllerManager::attach(const std::string& devicePath,
                              std::shared_ptr<DeviceProfile> profile) {
    auto reader = std::make_unique<EvdevEventReader>();
    if (!reader->open(devicePath)) {
        Logger::error("Failed to read controller: " + devicePath);
        return -1;
    }

    auto identity = reader->identity();
    if (isKnownVirtualPath(devicePath)) {
        Logger::info("Skipping our own virtual device: " + devicePath);
        return -1;
    }
    if (identity.serial.empty()) {
        // No kernel serial — fall back to which USB port the pad sits in, so two
        // identical pads stay distinguishable and re-plugging is reproducible.
        identity.pathKey = PortResolver::resolvePathKey(devicePath);
    }

    if (!profile) {
        profile = std::make_shared<DeviceProfile>();
        profile->identity = identity;

        std::string profilePath = findDeviceProfilePath(identity);
        if (access(profilePath.c_str(), R_OK) == 0) {
            if (!profile->loadFromFile(profilePath)) {
                Logger::warning("Failed to parse profile: " + profilePath);
            } else {
                Logger::info("Loaded profile: " + profilePath);
                profile->identity = identity;   // the file's copy may be stale; the live device wins
            }
        } else {
            Logger::info("No profile at " + profilePath + " — starting empty (run 'map' then 'save').");
        }
    }

    VirtualDeviceInfo info;
    info.name = profile->virtualIdentity.name;
    info.vendorId = profile->virtualIdentity.vendorId;
    info.productId = profile->virtualIdentity.productId;
    info.version = profile->virtualIdentity.version;

    auto virtualController = std::make_unique<XboxVirtualController>(std::make_unique<UInputBackend>());
    if (!virtualController->create(info)) {
        Logger::error("Failed to create virtual device for: " + devicePath);
        return -1;
    }
    std::string virtualPath = virtualController->outputDevicePath();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!virtualPath.empty()) knownVirtualPaths_.insert(virtualPath);
    }

    auto managed = std::make_unique<ManagedController>();
    managed->devicePath = devicePath;
    managed->deviceName = reader->deviceName();
    managed->profile = profile;
    managed->controller = std::make_unique<PhysicalController>(
        std::move(reader), std::move(virtualController), profile->mapping);

    PhysicalController* controllerRaw = managed->controller.get();
    if (profile->autoMap) controllerRaw->setActive(true);   // exclusive grab + own virtual device
    int id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        id = nextId_++;
        managed->id = id;
        byId_[id] = std::move(managed);
    }

    byId_[id]->runnerThread = std::thread([controllerRaw] { controllerRaw->run(); });
    Logger::info("Attached controller #" + std::to_string(id) + ": " +
                 byId_[id]->deviceName + " (" + devicePath + ")");
    return id;
}

void ControllerManager::detach(int id) {
    std::unique_ptr<ManagedController> entry;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = byId_.find(id);
        if (it == byId_.end()) {
            Logger::warning("No controller with id " + std::to_string(id));
            return;
        }
        entry = std::move(it->second);
        byId_.erase(it);
    }
    Logger::info("Detaching controller #" + std::to_string(id) + ": " + entry->deviceName);
    if (entry->controller) entry->controller->stop();
    if (entry->runnerThread.joinable()) entry->runnerThread.join();
}

void ControllerManager::detachAll() {
    std::vector<std::unique_ptr<ManagedController>> entries;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, entry] : byId_) entries.push_back(std::move(entry));
        byId_.clear();
    }
    for (auto& entry : entries) {
        if (entry->controller) entry->controller->stop();
        if (entry->runnerThread.joinable()) entry->runnerThread.join();
    }
}

PhysicalController* ControllerManager::get(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : it->second->controller.get();
}

ManagedController* ControllerManager::getManaged(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : it->second.get();
}

int ControllerManager::findByPath(const std::string& devicePath) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, entry] : byId_) {
        if (entry->devicePath == devicePath) return id;
    }
    return -1;
}

bool ControllerManager::isKnownVirtualPath(const std::string& devicePath) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return knownVirtualPaths_.count(devicePath) > 0;
}

std::set<std::string> ControllerManager::knownVirtualPaths() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return knownVirtualPaths_;
}

std::vector<int> ControllerManager::ids() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int> result;
    result.reserve(byId_.size());
    for (const auto& [id, entry] : byId_) result.push_back(id);
    std::sort(result.begin(), result.end());
    return result;
}