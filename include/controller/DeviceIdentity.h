#pragma once
#include <cstdint>
#include <string>

// The identity all of our virtual devices present as. We spoof a real wired Xbox
// 360 pad (045e:028e) so games that recognize Xbox controllers (SDL/Steam/Lutris)
// see a genuine pad without any vendor-whitelisting work. Because this is now a
// real identity, "is this our own virtual device?" can NOT be answered by vendor/
// product any longer — the code tracks the /dev/input/eventN node we created
// instead (see ControllerManager::knownVirtualPaths).
constexpr uint16_t kSpoofedVendorId  = 0x045e;   // Microsoft
constexpr uint16_t kSpoofedProductId = 0x028e;   // Xbox 360 wired controller
constexpr const char* kSpoofedName   = "Microsoft X-Box 360 pad";

// Physical device, as read from the evdev node at attach time.
struct DeviceIdentity {
    std::string name;
    uint16_t vendorId = 0;
    uint16_t productId = 0;
    std::string serial;     // often empty — not all pads report one
    std::string pathKey;    // USB-port-topology string, used only when serial is empty
};

// The uinput identity a given physical device presents as. Every device reports
// the same spoofed Xbox 360 identity — that's what makes games recognize it.
struct VirtualIdentity {
    std::string name = kSpoofedName;
    uint16_t vendorId = kSpoofedVendorId;
    uint16_t productId = kSpoofedProductId;
    uint16_t version = 1;
};