#include <cassert>
#include <cstdio>
#include <fstream>
#include <memory>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <linux/input.h>
#include "controller/Mapping.h"
#include "controller/LogicalControl.h"
#include "virtual/XboxVirtualController.h"

namespace {

class RecordingBackend : public OutputBackend {
public:
    struct RecordedAxis {
        int code;
        int minValue;
        int maxValue;

        bool matches(int expectedCode, int expectedMin, int expectedMax) const {
            return code == expectedCode && minValue == expectedMin && maxValue == expectedMax;
        }
    };

    std::vector<int> buttonCaps;
    std::vector<RecordedAxis> axisCaps;
    std::vector<int> sentButtons;   // pairs: code, value
    std::vector<int> sentAxes;

    void addButtonCapability(int code) override { buttonCaps.push_back(code); }
    void addAxisCapability(int code, int minValue, int maxValue) override {
        axisCaps.push_back({ code, minValue, maxValue });
    }
    bool create() override { return true; }
    void sendButton(int code, int value) override { sentButtons.push_back(code); sentButtons.push_back(value); }
    void sendAxis(int code, int value) override { sentAxes.push_back(code); sentAxes.push_back(value); }
};

void writeMappingFile(const char* path) {
    std::ofstream out(path);
    out << R"({
    "buttons": {
        "BTN_TRIGGER": ["Y"],
        "BTN_THUMB": ["B"],
        "BTN_THUMB2": ["A"],
        "BTN_TOP": ["X"],
        "BTN_TOP2": ["LB"],
        "BTN_PINKIE": ["RB"]
    },
    "axes": {
        "ABS_X": ["LeftStickX"],
        "ABS_Y": ["LeftStickY"],
        "ABS_Z": ["RightStickX"],
        "ABS_RZ": ["RightStickY"]
    }
})";
}

void writeDoubleMappingFile(const char* path) {
    std::ofstream out(path);
    out << R"({
    "buttons": {
        "BTN_TRIGGER": ["Y"],
        "BTN_THUMB": ["B", "Start"],
        "BTN_THUMB2": ["LeftTrigger"]
    },
    "axes": {}
})";
}

}  // namespace

int main() {
    // --- basic load / translate ---
    const char* path = "/tmp/mappingUnitTest.json";
    writeMappingFile(path);

    Mapping mapping;
    assert(mapping.loadFromFile(path));

    assert(mapping.translateButton(BTN_TRIGGER).size() == 1);
    assert(mapping.translateButton(BTN_TRIGGER)[0] == LogicalControl::Y);
    assert(mapping.translateButton(BTN_THUMB)[0]   == LogicalControl::B);
    assert(mapping.translateButton(BTN_THUMB2)[0]  == LogicalControl::A);
    assert(mapping.translateButton(BTN_TOP)[0]     == LogicalControl::X);
    assert(mapping.translateButton(BTN_TOP2)[0]    == LogicalControl::LB);
    assert(mapping.translateButton(BTN_PINKIE)[0]  == LogicalControl::RB);
    assert(mapping.translateButton(BTN_SELECT).empty());

    assert(mapping.translateAxis(ABS_X)[0]  == LogicalAxis::LeftStickX);
    assert(mapping.translateAxis(ABS_Y)[0]  == LogicalAxis::LeftStickY);
    assert(mapping.translateAxis(ABS_Z)[0]  == LogicalAxis::RightStickX);
    assert(mapping.translateAxis(ABS_RZ)[0] == LogicalAxis::RightStickY);
    assert(mapping.translateAxis(ABS_HAT0X).empty());

    // buttonAsAxis: BTN_THUMB2 → no buttonAsAxis yet
    assert(mapping.translateButtonAsAxis(BTN_THUMB2).empty());

    // --- XboxVirtualController buttons + axes (expanded set) ---
    auto backendOwner = std::make_unique<RecordingBackend>();
    RecordingBackend& backend = *backendOwner;
    XboxVirtualController vc(std::move(backendOwner));
    assert(vc.create());
    assert(backend.buttonCaps.size() == 15);   // 10 base + 4 D-pad + Home
    assert(backend.axisCaps.size() == 6);      // 4 sticks + 2 triggers

    auto findAxis = [&](int code) -> const RecordingBackend::RecordedAxis* {
        for (const auto& a : backend.axisCaps) if (a.code == code) return &a;
        return nullptr;
    };
    assert(findAxis(ABS_X)->matches(ABS_X, -32768, 32767));   // sticks: bipolar
    assert(findAxis(ABS_Y)->matches(ABS_Y, -32768, 32767));
    assert(findAxis(ABS_RX)->matches(ABS_RX, -32768, 32767));
    assert(findAxis(ABS_RY)->matches(ABS_RY, -32768, 32767));
    assert(findAxis(ABS_RZ)->matches(ABS_RZ, 0, 255));        // triggers: unipolar 0-255
    assert(findAxis(ABS_Z)->matches(ABS_Z, 0, 255));
    assert(findAxis(ABS_HAT0X) == nullptr);                   // D-pad is buttons, not hats
    assert(findAxis(ABS_HAT0Y) == nullptr);
    for (int cap : { BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT }) {
        assert(std::find(backend.buttonCaps.begin(), backend.buttonCaps.end(), cap) != backend.buttonCaps.end());
    }

    assert(vc.pressButton(LogicalControl::A, 1) == BTN_SOUTH);
    assert(backend.sentButtons.size() == 2);
    assert(backend.sentButtons[0] == BTN_SOUTH);
    assert(backend.sentButtons[1] == 1);

    assert(vc.pressButton(LogicalControl::RB, 0) == BTN_TR);
    assert(backend.sentButtons[2] == BTN_TR);
    assert(backend.sentButtons[3] == 0);

    assert(vc.pressButton(LogicalControl::Start, 1) == BTN_START);
    assert(vc.pressButton(LogicalControl::Unmapped, 1) == -1);

    assert(vc.pressButton(LogicalControl::DPadRight, 1) == BTN_DPAD_RIGHT);
    assert(backend.sentButtons.size() == 8);
    assert(backend.sentButtons[6] == BTN_DPAD_RIGHT);
    assert(backend.sentButtons[7] == 1);

    assert(vc.pressButton(LogicalControl::Home, 1) == BTN_MODE);
    assert(backend.sentButtons.size() == 10);
    assert(backend.sentButtons[8] == BTN_MODE);
    assert(backend.sentButtons[9] == 1);

    vc.setAxis(LogicalAxis::LeftStickX, 32767);
    assert(backend.sentAxes.size() == 2);
    assert(backend.sentAxes[0] == ABS_X);
    assert(backend.sentAxes[1] == 32767);

    vc.setAxis(LogicalAxis::LeftTrigger, 32767);
    assert(backend.sentAxes.size() == 4);
    assert(backend.sentAxes[2] == ABS_Z);
    assert(backend.sentAxes[3] == 32767);

    assert(XboxVirtualController::buttonCode(LogicalControl::A) == BTN_SOUTH);
    assert(XboxVirtualController::buttonCode(LogicalControl::Start) == BTN_START);
    assert(XboxVirtualController::buttonCode(LogicalControl::DPadUp) == BTN_DPAD_UP);
    assert(XboxVirtualController::buttonCode(LogicalControl::Home) == BTN_MODE);
    assert(XboxVirtualController::buttonCode(LogicalControl::Unmapped) == -1);
    assert(XboxVirtualController::axisCode(LogicalAxis::LeftTrigger) == ABS_Z);
    assert(XboxVirtualController::axisCode(LogicalAxis::RightStickY) == ABS_RY);
    assert(XboxVirtualController::axisCode(LogicalAxis::Invalid) == -1);

    // --- setButtonMapping / saveToFile round-trip ---
    const char* savePath = "/tmp/mappingSaveTest.json";
    mapping.addButtonMapping(BTN_TRIGGER, LogicalControl::LB);
    auto triggers = mapping.translateButton(BTN_TRIGGER);
    assert(triggers.size() == 2);
    assert(triggers[0] == LogicalControl::Y);
    assert(triggers[1] == LogicalControl::LB);
    assert(mapping.saveToFile(savePath));

    Mapping reloaded;
    assert(reloaded.loadFromFile(savePath));
    auto reloadedTrigger = reloaded.translateButton(BTN_TRIGGER);
    assert(reloadedTrigger.size() == 2);
    assert(reloadedTrigger[0] == LogicalControl::Y);
    assert(reloadedTrigger[1] == LogicalControl::LB);
    assert(reloaded.translateButton(BTN_THUMB)[0] == LogicalControl::B);
    assert(reloaded.translateAxis(ABS_X)[0] == LogicalAxis::LeftStickX);

    // --- double-mapping: one button → two logical controls ---
    const char* doublePath = "/tmp/mappingDoubleTest.json";
    writeDoubleMappingFile(doublePath);
    Mapping doubleMapping;
    assert(doubleMapping.loadFromFile(doublePath));
    auto thumbTargets = doubleMapping.translateButton(BTN_THUMB);
    assert(thumbTargets.size() == 2);
    assert(thumbTargets[0] == LogicalControl::B);
    assert(thumbTargets[1] == LogicalControl::Start);

    auto thumb2Axes = doubleMapping.translateButtonAsAxis(BTN_THUMB2);
    assert(thumb2Axes.size() == 1);
    assert(thumb2Axes[0] == LogicalAxis::LeftTrigger);

    // --- additive add deduplicates same logical ---
    int before = (int)doubleMapping.translateButton(BTN_THUMB).size();
    doubleMapping.addButtonMapping(BTN_THUMB, LogicalControl::B);  // duplicate — should be ignored
    int after = (int)doubleMapping.translateButton(BTN_THUMB).size();
    assert(after == before);

    // --- empty axes table must round-trip (was: "axes": null, crashed on reload) ---
    {
        Mapping empty;
        empty.addButtonMapping(BTN_TRIGGER, LogicalControl::A);  // no axes
        const char* emptyPath = "/tmp/mappingEmptyAxesTest.json";
        assert(empty.saveToFile(emptyPath));
        Mapping emptyReloaded;
        assert(emptyReloaded.loadFromFile(emptyPath));
        assert(emptyReloaded.translateButton(BTN_TRIGGER).size() == 1);
        assert(emptyReloaded.translateAxis(ABS_X).empty());
        remove(emptyPath);
    }

    // --- clear() drops everything (guided walk starts from a fresh layout) ---
    {
        Mapping gone;
        gone.addButtonMapping(BTN_TRIGGER, LogicalControl::A);
        gone.addButtonAsAxisMapping(BTN_THUMB, LogicalAxis::LeftTrigger);
        gone.addAxisMapping(ABS_X, LogicalAxis::LeftStickX);
        gone.clear();
        assert(gone.translateButton(BTN_TRIGGER).empty());
        assert(gone.translateButtonAsAxis(BTN_THUMB).empty());
        assert(gone.translateAxis(ABS_X).empty());
    }

    // --- saveToFile must create a missing parent directory ---
    {
        Mapping m;
        m.addButtonMapping(BTN_TRIGGER, LogicalControl::A);
        m.addAxisMapping(ABS_X, LogicalAxis::LeftStickX);

        const char* nestedPath = "/tmp/mappingNested/will/not/exist/yet/usb.json";
        assert(m.saveToFile(nestedPath));   // creates mappings/xbox/... style dirs

        Mapping loaded;
        assert(loaded.loadFromFile(nestedPath));
        assert(loaded.translateButton(BTN_TRIGGER).size() == 1);
        assert(loaded.translateAxis(ABS_X)[0] == LogicalAxis::LeftStickX);
    }

    // --- saveToFile reports failure when the parent cannot be created ---
    {
        const char* blockingParent = "/tmp/mappingBlockingParent";
        std::ofstream blocker(blockingParent);   // a regular file, not a directory
        blocker.close();

        Mapping m;
        m.addButtonMapping(BTN_TRIGGER, LogicalControl::A);
        assert(!m.saveToFile(std::string(blockingParent) + "/x.json"));
        std::remove(blockingParent);
    }

    // --- profile pathFor ---
    assert(Mapping::pathFor("xbox", "usb") == "mappings/xbox/usb.json");
    assert(Mapping::pathFor("ps", "dualshock") == "mappings/ps/dualshock.json");

    // --- replacement semantics: one physical source per logical target ---
    {
        Mapping m;
        m.addButtonMapping(BTN_TRIGGER, LogicalControl::B);
        PhysicalSource first = m.sourceFor(LogicalControl::B).value();
        assert(first == buttonSource(BTN_TRIGGER));

        m.addButtonMapping(BTN_THUMB, LogicalControl::B);   // rebind B elsewhere
        assert(m.sourceFor(LogicalControl::B).value() == buttonSource(BTN_THUMB));
        assert(m.translateButton(BTN_TRIGGER).empty());     // old binding is gone
    }

    // --- hats: per-direction D-pad buttons + per-direction analog, reverse lookup ---
    {
        Mapping m;
        m.addMapping(hatSource(ABS_HAT0Y, -1), LogicalControl::DPadUp);
        m.addHatAsAxisMapping(ABS_HAT0X, +1, LogicalAxis::RightStickX);

        assert(m.translateHat(ABS_HAT0Y, -1).size() == 1);
        assert(m.translateHat(ABS_HAT0Y, -1)[0] == LogicalControl::DPadUp);
        assert(m.translateHat(ABS_HAT0Y, +1).empty());                    // other direction unbound
        assert(m.translateHat(ABS_HAT0X, +1).empty());                    // analog-only binding
        assert(m.translateHatAsAxis(ABS_HAT0X, +1)[0] == LogicalAxis::RightStickX);

        assert(m.sourceFor(LogicalControl::DPadUp).value() == hatSource(ABS_HAT0Y, -1));
        assert(m.sourceFor(LogicalAxis::RightStickX).value() == hatSource(ABS_HAT0X, +1));

        m.clearTarget(LogicalControl::DPadUp);
        assert(!m.sourceFor(LogicalControl::DPadUp));
        assert(m.translateHat(ABS_HAT0Y, -1).empty());
    }

    // --- analog axis driving a digital target (thresholded) ---
    {
        Mapping m;
        m.addMapping(axisSource(ABS_X), LogicalControl::A);
        assert(m.controlTargets(axisSource(ABS_X))[0] == LogicalControl::A);
        assert(m.sourceFor(LogicalControl::A).value() == axisSource(ABS_X));
        assert(m.translateButton(ABS_X).empty());   // no button row touched

        m.clearTarget(LogicalControl::A);
        assert(m.controlTargets(axisSource(ABS_X)).empty());
    }

    // --- save/load round-trip with hats + analog->digital + double mapping ---
    {
        Mapping m;
        m.addMapping(hatSource(ABS_HAT0Y, -1), LogicalControl::DPadUp);
        m.addMapping(axisSource(ABS_X), LogicalAxis::LeftStickX);
        m.addMapping(axisSource(ABS_X), LogicalControl::A);       // analog -> digital
        m.addMapping(buttonSource(BTN_TRIGGER), LogicalControl::Y);
        m.addMapping(buttonSource(BTN_TRIGGER), LogicalControl::LB);   // double mapping

        const char* mixedPath = "/tmp/mappingMixedTest.json";
        assert(m.saveToFile(mixedPath));

        Mapping loaded;
        assert(loaded.loadFromFile(mixedPath));
        assert(loaded.translateHat(ABS_HAT0Y, -1)[0] == LogicalControl::DPadUp);
        assert(loaded.translateAxis(ABS_X)[0] == LogicalAxis::LeftStickX);
        assert(loaded.controlTargets(axisSource(ABS_X))[0] == LogicalControl::A);
        auto btn = loaded.translateButton(BTN_TRIGGER);
        assert(btn.size() == 2);
        assert(btn[0] == LogicalControl::Y);
        assert(btn[1] == LogicalControl::LB);
        remove(mixedPath);
    }

    // --- last-wins on load: the same target bound twice ends up at the last source ---
    {
        const char* dupPath = "/tmp/mappingDupLoadTest.json";
        {
            Mapping writer;
            writer.addMapping(buttonSource(BTN_TRIGGER), LogicalControl::A);
            writer.addMapping(buttonSource(BTN_THUMB), LogicalControl::B);
            assert(writer.saveToFile(dupPath));
        }
        {
            // hand-edit: two buttons both claim "A". nlohmann iterates object keys
            // in sorted order ("BTN_THUMB" < "BTN_TRIGGER"), so the loader's
            // last-wins rule deterministically keeps BTN_TRIGGER as A's source.
            std::ofstream out(dupPath);
            out << R"({
                "buttons": {
                    "BTN_THUMB": ["A"],
                    "BTN_TRIGGER": ["A"]
                }
            })";
        }
        Mapping m;
        assert(m.loadFromFile(dupPath));
        assert(m.translateButton(BTN_THUMB).empty());                    // A removed from BTN_THUMB
        assert(m.translateButton(BTN_TRIGGER)[0] == LogicalControl::A);  // ...it landed on BTN_TRIGGER
        remove(dupPath);
    }

    // --- legacy migration: profiles written while the D-pad was a hat-axis pair
    //     (axes.ABS_HAT0X = ["DPadX"]) must load back as the four directional
    //     buttons — no player re-binding needed.
    {
        const char* legacyHatPath = "/tmp/mappingLegacyDpadAxes.json";
        std::ofstream out(legacyHatPath);
        out << R"({
            "buttons": {},
            "axes": {
                "ABS_HAT0X": ["DPadX"],
                "ABS_HAT0Y": ["DPadY"]
            },
            "hats": {}
        })";
        out.close();

        Mapping migrated;
        assert(migrated.loadFromFile(legacyHatPath));
        assert(migrated.translateHat(ABS_HAT0X, -1).size() == 1);
        assert(migrated.translateHat(ABS_HAT0X, -1)[0] == LogicalControl::DPadLeft);
        assert(migrated.translateHat(ABS_HAT0X, +1)[0] == LogicalControl::DPadRight);
        assert(migrated.translateHat(ABS_HAT0Y, -1)[0] == LogicalControl::DPadUp);
        assert(migrated.translateHat(ABS_HAT0Y, +1)[0] == LogicalControl::DPadDown);
        assert(migrated.translateAxis(ABS_HAT0X).empty());                // no leftover axis mapping
        assert(migrated.translateAxis(ABS_HAT0Y).empty());
        remove(legacyHatPath);
    }

    // --- native format: directional hat entries (one button per direction)
    //     round-trip unchanged, no conversion applied ---
    {
        const char* nativeHatPath = "/tmp/mappingNativeDpad.json";
        std::ofstream out(nativeHatPath);
        out << R"({
            "buttons": {},
            "axes": {},
            "hats": {
                "ABS_HAT0X": { "-1": ["DPadLeft"], "1": ["DPadRight"] },
                "ABS_HAT0Y": { "-1": ["DPadUp"],   "1": ["DPadDown"] }
            }
        })";
        out.close();

        Mapping native_;
        assert(native_.loadFromFile(nativeHatPath));
        assert(native_.translateHat(ABS_HAT0X, -1)[0] == LogicalControl::DPadLeft);
        assert(native_.translateHat(ABS_HAT0X, +1)[0] == LogicalControl::DPadRight);
        assert(native_.translateHat(ABS_HAT0Y, -1)[0] == LogicalControl::DPadUp);
        assert(native_.translateHat(ABS_HAT0Y, +1)[0] == LogicalControl::DPadDown);
        remove(nativeHatPath);
    }

    remove(path);
    remove(savePath);
    remove(doublePath);
    std::filesystem::remove_all("/tmp/mappingNested");
    printf("mappingUnitTest: all assertions passed\n");
    return 0;
}
