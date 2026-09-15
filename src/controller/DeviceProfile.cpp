#include "controller/DeviceProfile.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <system_error>
#include "Logger.h"

using json = nlohmann::json;

namespace {

// by-path names contain ':' and other characters unsafe in filenames on some setups.
std::string sanitize(std::string s) {
    std::replace_if(s.begin(), s.end(), [](char c) { return c == ':' || c == '/'; }, '_');
    return s;
}

}  // namespace

std::string DeviceProfile::keyFor(const DeviceIdentity& id) {
    std::ostringstream oss;
    oss << std::hex << std::setw(4) << std::setfill('0') << id.vendorId << "_"
        << std::hex << std::setw(4) << std::setfill('0') << id.productId;

    if (!id.serial.empty()) {
        oss << "_" << id.serial;
    } else if (!id.pathKey.empty()) {
        oss << "_" << sanitize(id.pathKey);
    }
    return oss.str();
}

std::string DeviceProfile::pathFor(const DeviceIdentity& id) {
    return "devices/" + keyFor(id) + ".json";
}

bool DeviceProfile::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    json j;
    file >> j;

    identity.name = j.value("deviceName", "");
    identity.vendorId = j.value("vendorId", 0);
    identity.productId = j.value("productId", 0);
    identity.serial = j.value("serial", "");

    virtualIdentity.name = j.value("virtualName", kSpoofedName);
    virtualIdentity.vendorId = j.value("virtualVendorId", kSpoofedVendorId);
    virtualIdentity.productId = j.value("virtualProductId", kSpoofedProductId);
    virtualIdentity.version = j.value("virtualVersion", 1);

    // Profiles saved by an earlier build used a reserved 0x1234 vendor and a
    // per-device product id. Now that every virtual device spoofs a real Xbox 360
    // pad, such a file must be re-based to the spoofed identity or games would
    // stop recognizing the pad.
    if (virtualIdentity.vendorId == 0x1234 || virtualIdentity.name == "Mapped Virtual Pad") {
        virtualIdentity.name = kSpoofedName;
        virtualIdentity.vendorId = kSpoofedVendorId;
        virtualIdentity.productId = kSpoofedProductId;
    }

    autoMap = j.value("autoMap", false);

    if (j.contains("mapping")) {
        mapping.loadFromJson(j["mapping"]);
    }
    return true;
}

bool DeviceProfile::saveToFile(const std::string& path) const {
    json j;
    j["deviceName"] = identity.name;
    j["vendorId"] = identity.vendorId;
    j["productId"] = identity.productId;
    j["serial"] = identity.serial;

    j["virtualName"] = virtualIdentity.name;
    j["virtualVendorId"] = virtualIdentity.vendorId;
    j["virtualProductId"] = virtualIdentity.productId;
    j["virtualVersion"] = virtualIdentity.version;

    j["autoMap"] = autoMap;
    j["mapping"] = mapping.toJson();

    std::filesystem::path fsPath(path);
    if (fsPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(fsPath.parent_path(), ec);
        if (ec) {
            Logger::error("Could not create directory " + fsPath.parent_path().string() +
                          ": " + ec.message());
            return false;
        }
    }

    std::ofstream file(path);
    if (!file.is_open()) {
        Logger::error("Could not open " + path + " for writing");
        return false;
    }
    file << j.dump(4);
    return true;
}