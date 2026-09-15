#include "controller/Mapping.h"
#include "Logger.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <set>
#include <linux/input.h>

using json = nlohmann::json;

static const std::unordered_map<std::string, int> nameToButtonCode = {
    { "BTN_TRIGGER", BTN_TRIGGER }, { "BTN_THUMB",  BTN_THUMB },
    { "BTN_THUMB2",  BTN_THUMB2 },  { "BTN_TOP",    BTN_TOP },
    { "BTN_TOP2",    BTN_TOP2 },    { "BTN_PINKIE", BTN_PINKIE },
    { "BTN_BASE",    BTN_BASE },    { "BTN_BASE2",  BTN_BASE2 },
    { "BTN_BASE3",   BTN_BASE3 },   { "BTN_BASE4",  BTN_BASE4 },
    { "BTN_BASE5",   BTN_BASE5 },   { "BTN_BASE6",  BTN_BASE6 },
    { "BTN_SOUTH", BTN_SOUTH }, { "BTN_EAST", BTN_EAST },
    { "BTN_WEST", BTN_WEST },   { "BTN_NORTH", BTN_NORTH },
    { "BTN_TL", BTN_TL }, { "BTN_TR", BTN_TR },
    { "BTN_TL2", BTN_TL2 }, { "BTN_TR2", BTN_TR2 },
    { "BTN_SELECT", BTN_SELECT }, { "BTN_START", BTN_START },
    { "BTN_MODE", BTN_MODE },
    { "BTN_THUMBL", BTN_THUMBL }, { "BTN_THUMBR", BTN_THUMBR },
    { "BTN_DPAD_UP", BTN_DPAD_UP }, { "BTN_DPAD_DOWN", BTN_DPAD_DOWN },
    { "BTN_DPAD_LEFT", BTN_DPAD_LEFT }, { "BTN_DPAD_RIGHT", BTN_DPAD_RIGHT },
};

static const std::unordered_map<std::string, int> nameToAxisCode = {
    { "ABS_X", ABS_X }, { "ABS_Y", ABS_Y }, { "ABS_Z", ABS_Z },
    { "ABS_RX", ABS_RX }, { "ABS_RY", ABS_RY }, { "ABS_RZ", ABS_RZ },
    { "ABS_HAT0X", ABS_HAT0X }, { "ABS_HAT0Y", ABS_HAT0Y },
};

static const std::unordered_map<int, std::string>& buttonCodeToName() {
    static const auto reversed = [] {
        std::unordered_map<int, std::string> m;
        for (auto& [n, c] : nameToButtonCode) m[c] = n;
        return m;
    }();
    return reversed;
}

static const std::unordered_map<int, std::string>& axisCodeToName() {
    static const auto reversed = [] {
        std::unordered_map<int, std::string> m;
        for (auto& [n, c] : nameToAxisCode) m[c] = n;
        return m;
    }();
    return reversed;
}

namespace {

// Erases `target` from every source vector; drops sources left with no targets.
void eraseControlFromAll(std::unordered_map<PhysicalSource, std::vector<LogicalControl>,
                                            PhysicalSourceHash>& map,
                         LogicalControl target) {
    for (auto it = map.begin(); it != map.end();) {
        auto& v = it->second;
        v.erase(std::remove(v.begin(), v.end(), target), v.end());
        if (v.empty()) it = map.erase(it); else ++it;
    }
}

void eraseAxisFromAll(std::unordered_map<PhysicalSource, std::vector<LogicalAxis>,
                                         PhysicalSourceHash>& map,
                      LogicalAxis target) {
    for (auto it = map.begin(); it != map.end();) {
        auto& v = it->second;
        v.erase(std::remove(v.begin(), v.end(), target), v.end());
        if (v.empty()) it = map.erase(it); else ++it;
    }
}

// Adds `target` to `vec` unless already present.
void pushUnique(std::vector<LogicalControl>& vec, LogicalControl target) {
    if (std::find(vec.begin(), vec.end(), target) == vec.end()) vec.push_back(target);
}
void pushUnique(std::vector<LogicalAxis>& vec, LogicalAxis target) {
    if (std::find(vec.begin(), vec.end(), target) == vec.end()) vec.push_back(target);
}

// Parses one JSON target string into either a control or an axis, dispatching
// to the right store. Unknown names are warned about.
void addParsedTarget(const std::string& target, const PhysicalSource& src,
                     std::unordered_map<PhysicalSource, std::vector<LogicalControl>, PhysicalSourceHash>& controls,
                     std::unordered_map<PhysicalSource, std::vector<LogicalAxis>, PhysicalSourceHash>& axes) {
    LogicalControl lc = logicalControlFromName(target);
    if (lc != LogicalControl::Unmapped) {
        eraseControlFromAll(controls, lc);
        pushUnique(controls[src], lc);
        return;
    }
    LogicalAxis la = logicalAxisFromName(target);
    if (la != LogicalAxis::Invalid) {
        eraseAxisFromAll(axes, la);
        pushUnique(axes[src], la);
        return;
    }
    Logger::warning("Unknown logical target in JSON: " + target);
}

}  // namespace

std::string Mapping::pathFor(const std::string& target, const std::string& source) {
    return "mappings/" + target + "/" + source + ".json";
}

Mapping::Mapping(Mapping&& other) noexcept
    : controlBySource(std::move(other.controlBySource)),
      axisBySource(std::move(other.axisBySource)) {}

Mapping& Mapping::operator=(Mapping&& other) noexcept {
    if (this == &other) return *this;
    std::scoped_lock lock(mutex_, other.mutex_);
    controlBySource = std::move(other.controlBySource);
    axisBySource    = std::move(other.axisBySource);
    return *this;
}

Mapping::Mapping(const Mapping& other) {
    std::lock_guard<std::mutex> lock(other.mutex_);
    controlBySource = other.controlBySource;
    axisBySource = other.axisBySource;
}

Mapping& Mapping::operator=(const Mapping& other) {
    if (this == &other) return *this;
    std::scoped_lock lock(mutex_, other.mutex_);
    controlBySource = other.controlBySource;
    axisBySource = other.axisBySource;
    return *this;
}

void Mapping::addMapping(const PhysicalSource& source, LogicalControl logical) {
    std::lock_guard<std::mutex> lock(mutex_);
    eraseControlFromAll(controlBySource, logical);
    pushUnique(controlBySource[source], logical);
}

void Mapping::addMapping(const PhysicalSource& source, LogicalAxis axis) {
    std::lock_guard<std::mutex> lock(mutex_);
    eraseAxisFromAll(axisBySource, axis);
    pushUnique(axisBySource[source], axis);
}

void Mapping::addButtonMapping(int physicalCode, LogicalControl logical) {
    addMapping(buttonSource(physicalCode), logical);
}

void Mapping::addButtonAsAxisMapping(int physicalCode, LogicalAxis axis) {
    addMapping(buttonSource(physicalCode), axis);
}

void Mapping::addAxisMapping(int physicalCode, LogicalAxis axis) {
    addMapping(axisSource(physicalCode), axis);
}

void Mapping::addHatMapping(int code, int dir, LogicalControl logical) {
    addMapping(hatSource(code, dir), logical);
}

void Mapping::addHatAsAxisMapping(int code, int dir, LogicalAxis axis) {
    addMapping(hatSource(code, dir), axis);
}

void Mapping::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    controlBySource.clear();
    axisBySource.clear();
}

size_t Mapping::mappingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return controlBySource.size() + axisBySource.size();
}

std::vector<LogicalControl> Mapping::controlTargets(const PhysicalSource& source) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = controlBySource.find(source);
    return it != controlBySource.end() ? it->second : std::vector<LogicalControl>{};
}

std::vector<LogicalAxis> Mapping::axisTargets(const PhysicalSource& source) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = axisBySource.find(source);
    return it != axisBySource.end() ? it->second : std::vector<LogicalAxis>{};
}

std::vector<LogicalControl> Mapping::translateButton(int physicalCode) const {
    return controlTargets(buttonSource(physicalCode));
}

std::vector<LogicalAxis> Mapping::translateButtonAsAxis(int physicalCode) const {
    return axisTargets(buttonSource(physicalCode));
}

std::vector<LogicalAxis> Mapping::translateAxis(int physicalCode) const {
    return axisTargets(axisSource(physicalCode));
}

std::vector<LogicalControl> Mapping::translateHat(int code, int dir) const {
    return controlTargets(hatSource(code, dir));
}

std::vector<LogicalAxis> Mapping::translateHatAsAxis(int code, int dir) const {
    return axisTargets(hatSource(code, dir));
}

std::optional<PhysicalSource> Mapping::sourceFor(LogicalControl logical) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [src, vec] : controlBySource) {
        if (std::find(vec.begin(), vec.end(), logical) != vec.end()) return src;
    }
    return std::nullopt;
}

std::optional<PhysicalSource> Mapping::sourceFor(LogicalAxis axis) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [src, vec] : axisBySource) {
        if (std::find(vec.begin(), vec.end(), axis) != vec.end()) return src;
    }
    return std::nullopt;
}

void Mapping::clearTarget(LogicalControl logical) {
    std::lock_guard<std::mutex> lock(mutex_);
    eraseControlFromAll(controlBySource, logical);
}

void Mapping::clearTarget(LogicalAxis axis) {
    std::lock_guard<std::mutex> lock(mutex_);
    eraseAxisFromAll(axisBySource, axis);
}

bool Mapping::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        Logger::error("Could not open mapping file: " + path);
        return false;
    }

    json j;
    file >> j;

    if (!loadFromJson(j)) {
        Logger::error("Failed to parse mapping: " + path);
        return false;
    }
    Logger::info("Loaded mapping from " + path);
    return true;
}

bool Mapping::loadFromJson(const nlohmann::json& j) {
    // Migrate D-pad hat-axis bindings (axes.ABS_HAT0X = ["DPadX"]) back to the
    // button-based D-pad: hats.ABS_HAT0X."-1" = ["DPadLeft"], "1" = ["DPadRight"].
    // Profiles written while the D-pad was a hat-axis pair get rebuilt into the
    // four directional buttons without any player re-binding.
    nlohmann::json migrated = j;
    if (migrated.contains("axes") && migrated["axes"].is_object()) {
        std::vector<std::string> axisKeys;
        for (auto& [name, targets] : migrated["axes"].items()) axisKeys.push_back(name);
        for (const auto& name : axisKeys) {
            auto codeIt = nameToAxisCode.find(name);
            if (codeIt == nameToAxisCode.end() || !isHatAxisCode(codeIt->second)) continue;
            auto& targets = migrated["axes"][name];
            if (!targets.is_array()) continue;
            std::vector<std::string> remaining;
            json& hatDirs = migrated["hats"][name];
            if (!hatDirs.is_object()) hatDirs = json::object();
            for (const auto& t : targets) {
                const std::string target = t.get<std::string>();
                if (target == "DPadX") {
                    hatDirs["-1"].push_back("DPadLeft");
                    hatDirs["1"].push_back("DPadRight");
                } else if (target == "DPadY") {
                    hatDirs["-1"].push_back("DPadUp");
                    hatDirs["1"].push_back("DPadDown");
                } else {
                    remaining.push_back(target);
                }
            }
            if (remaining.empty()) migrated["axes"].erase(name);
            else targets = remaining;
        }
    }

    std::unordered_map<PhysicalSource, std::vector<LogicalControl>, PhysicalSourceHash> loadedControls;
    std::unordered_map<PhysicalSource, std::vector<LogicalAxis>, PhysicalSourceHash> loadedAxes;

    if (migrated.contains("buttons") && migrated["buttons"].is_object()) {
        for (const auto& [physName, targets] : migrated["buttons"].items()) {
            auto codeIt = nameToButtonCode.find(physName);
            if (codeIt == nameToButtonCode.end()) {
                Logger::warning("Unknown physical button in JSON: " + physName);
                continue;
            }
            for (const auto& target : targets) {
                addParsedTarget(target.get<std::string>(), buttonSource(codeIt->second),
                                loadedControls, loadedAxes);
            }
        }
    }

    if (migrated.contains("axes") && migrated["axes"].is_object()) {
        for (const auto& [physName, targets] : migrated["axes"].items()) {
            auto codeIt = nameToAxisCode.find(physName);
            if (codeIt == nameToAxisCode.end()) {
                Logger::warning("Unknown physical axis in JSON: " + physName);
                continue;
            }
            for (const auto& target : targets) {
                addParsedTarget(target.get<std::string>(), axisSource(codeIt->second),
                                loadedControls, loadedAxes);
            }
        }
    }

    if (migrated.contains("hats") && migrated["hats"].is_object()) {
        for (const auto& [hatName, dirs] : migrated["hats"].items()) {
            auto codeIt = nameToAxisCode.find(hatName);
            if (codeIt == nameToAxisCode.end()) {
                Logger::warning("Unknown physical hat in JSON: " + hatName);
                continue;
            }
            for (const auto& [dirStr, targets] : dirs.items()) {
                if (dirStr != "-1" && dirStr != "1") continue;
                int dir = dirStr == "-1" ? -1 : 1;
                for (const auto& target : targets) {
                    addParsedTarget(target.get<std::string>(), hatSource(codeIt->second, dir),
                                    loadedControls, loadedAxes);
                }
            }
        }
    }

    size_t controlCount, axisCount;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        controlBySource = std::move(loadedControls);
        axisBySource = std::move(loadedAxes);
        controlCount = controlBySource.size();
        axisCount = axisBySource.size();
    }

    Logger::info("Parsed mapping (" + std::to_string(controlCount) +
                 " digital sources, " + std::to_string(axisCount) + " analog sources)");
    return true;
}

nlohmann::json Mapping::toJson() const {
    json j;
    j["buttons"] = json::object();
    j["axes"] = json::object();
    j["hats"] = json::object();
    json& buttonsObj = j["buttons"];
    json& axesObj = j["axes"];
    json& hatsObj = j["hats"];

    std::lock_guard<std::mutex> lock(mutex_);
    auto& bNames = buttonCodeToName();
    auto& aNames = axisCodeToName();

    // Appends the control+axis target names of one source to the right JSON slot.
    auto appendTags = [&](const PhysicalSource& src,
                          const std::vector<LogicalControl>* controls,
                          const std::vector<LogicalAxis>* axes) {
        auto tags = [&](json& arr) {
            if (controls) {
                for (auto lc : *controls) arr.push_back(logicalControlName(lc));
            }
            if (axes) {
                for (auto la : *axes) arr.push_back(logicalAxisName(la));
            }
        };
        if (src.kind == SourceKind::Button) {
            auto it = bNames.find(src.code);
            if (it == bNames.end()) return;
            tags(buttonsObj[it->second]);
        } else if (src.kind == SourceKind::Axis) {
            auto it = aNames.find(src.code);
            if (it == aNames.end()) return;
            tags(axesObj[it->second]);
        } else {   // Hat
            auto it = aNames.find(src.code);
            if (it == aNames.end()) return;
            json& dirObj = hatsObj[it->second];
            if (!dirObj.is_object()) dirObj = json::object();
            tags(dirObj[std::to_string(src.dir)]);
        }
    };

    for (const auto& [src, controls] : controlBySource) {
        if (controls.empty()) continue;
        appendTags(src, &controls, nullptr);
    }
    for (const auto& [src, axes] : axisBySource) {
        if (axes.empty()) continue;
        appendTags(src, nullptr, &axes);
    }
    return j;
}

bool Mapping::saveToFile(const std::string& path) const {
    json j = toJson();

    std::filesystem::path fsPath(path);
    if (fsPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(fsPath.parent_path(), ec);
        if (ec) {
            Logger::error("Could not create directory " + fsPath.parent_path().string() + ": " + ec.message());
            return false;
        }
    }

    std::ofstream file(path);
    if (!file.is_open()) {
        Logger::error("Could not open " + path + " for writing");
        return false;
    }
    file << j.dump(4);
    Logger::info("Saved mapping to " + path);
    return true;
}