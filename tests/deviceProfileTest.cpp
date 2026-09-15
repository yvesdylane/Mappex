// DeviceProfile: identity-key derivation, per-device virtual id derivation, and
// JSON round-trip (including the nested mapping with buttons/axes/hats).
#include "controller/DeviceProfile.h"
#include <linux/input.h>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>

int main() {
    // keyFor precedence: serial beats by-path; missing serial falls back to the
    // sanitised USB-port path; neither -> plain vendor_product.
    DeviceIdentity withSerial;
    withSerial.vendorId = 0x045e;
    withSerial.productId = 0x028e;
    withSerial.serial = "SN12345";
    assert(DeviceProfile::keyFor(withSerial) == "045e_028e_SN12345");

    DeviceIdentity noSerial;
    noSerial.vendorId = 0x045e;
    noSerial.productId = 0x028e;
    noSerial.pathKey = "pci-0000:00:14.0-usb-0:1.2:1.0-event-joystick";
    assert(DeviceProfile::keyFor(noSerial) ==
           "045e_028e_pci-0000_00_14.0-usb-0_1.2_1.0-event-joystick");

    DeviceIdentity plain;
    plain.vendorId = 0x810;
    plain.productId = 0x1;
    assert(DeviceProfile::keyFor(plain) == "0810_0001");

    // Full round trip: identity + virtual identity + autoMap + nested mapping.
    // The virtual pad now spoofs a real Xbox 360 identity by default.
    DeviceProfile profile;
    profile.identity = withSerial;
    profile.virtualIdentity.vendorId = 0x045e;
    profile.virtualIdentity.productId = 0x028e;
    profile.virtualIdentity.name = "Microsoft X-Box 360 pad";
    profile.autoMap = true;
    profile.mapping.addMapping(buttonSource(BTN_SOUTH), LogicalControl::A);
    profile.mapping.addMapping(axisSource(ABS_X), LogicalAxis::LeftStickX);
    profile.mapping.addMapping(hatSource(ABS_HAT0Y, -1), LogicalControl::DPadUp);
    profile.mapping.addMapping(hatSource(ABS_HAT0X, 1), LogicalControl::DPadRight);

    std::string path = DeviceProfile::pathFor(withSerial);
    assert(profile.saveToFile(path));
    assert(!profile.saveToFile("/nonexistent-dir-xyz/" + DeviceProfile::keyFor(withSerial) + ".json") && "unwritable path must fail");

    DeviceProfile loaded;
    assert(loaded.loadFromFile(path));
    assert(loaded.identity.vendorId == withSerial.vendorId);
    assert(loaded.identity.productId == withSerial.productId);
    assert(loaded.identity.serial == withSerial.serial);
    assert(loaded.autoMap == true);
    assert(loaded.mapping.sourceFor(LogicalControl::A) == buttonSource(BTN_SOUTH));
    assert(loaded.mapping.sourceFor(LogicalAxis::LeftStickX) == axisSource(ABS_X));
    assert(loaded.mapping.sourceFor(LogicalControl::DPadUp) == hatSource(ABS_HAT0Y, -1));
    assert(loaded.mapping.sourceFor(LogicalControl::DPadRight) == hatSource(ABS_HAT0X, 1));

    // Legacy migration: a profile saved when the virtual device used vendor
    // 0x1234 under the "Mapped ..." name is re-based onto the spoofed identity.
    {
        std::string legacyPath = "devices/045e_028e_LEGACY.json";
        {
            std::ofstream out(legacyPath);
            out << R"({
                "virtualVendorId": 4660,
                "virtualProductId": 46781,
                "virtualName": "Mapped  USB Gamepad          ",
                "virtualVersion": 1,
                "autoMap": true
            })";
        }
        DeviceProfile legacy;
        assert(legacy.loadFromFile(legacyPath));
        assert(legacy.virtualIdentity.vendorId == 0x045e && "legacy vendor 0x1234 must migrate to spoofed Xbox id");
        assert(legacy.virtualIdentity.productId == 0x028e);
        assert(legacy.virtualIdentity.name == "Microsoft X-Box 360 pad" && "legacy 'Mapped ...' name must migrate");
        std::filesystem::remove(legacyPath);
    }

    printf("deviceProfileTest: all assertions passed\n");
    return 0;
}