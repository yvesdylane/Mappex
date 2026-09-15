#pragma once
#include <string>
#include <cstdint>
#include "controller/DeviceIdentity.h"
#include "controller/Mapping.h"

// One JSON file per physical device, keyed on vendor/product(+serial, or the USB
// port when there is no serial). Holds the physical identity (so re-plugging
// reproduces it) plus the mapping and whether to auto-map on connect.
class DeviceProfile {
public:
    static std::string keyFor(const DeviceIdentity& id);          // sanitized vendor_product[_serial|_pathKey]
    static std::string pathFor(const DeviceIdentity& id);         // devices/<key>.json

    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path) const;

    DeviceIdentity identity;
    VirtualIdentity virtualIdentity;
    bool autoMap = false;
    Mapping mapping;   // persistence copy; runtime edits happen in the controller
};