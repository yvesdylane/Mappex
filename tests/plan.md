### 1
Environment & permissions
Install libevdev and nlohmann-json (via your package manager or vcpkg/conan). Set up a udev rule so your user can read /dev/input/eventX and write to /dev/uinput without sudo. Confirm this works with a tiny test program that just opens your controller's event file and prints raw bytes.
### 2
Raw event reader (no abstractions yet)
Write a single throwaway program that uses libevdev to open your physical controller and print every button/axis event to the console when you press things. This proves your evdev pipeline works before you build anything around it. Don't create classes yet — this is a spike.
### 3
Minimal virtual controller output
Write a second throwaway program that creates a uinput virtual gamepad and fakes a single button press (e.g. BTN_SOUTH). Verify it shows up as a device (jstest-gtk or evtest) and that a game/browser gamepad tester sees it. This proves your output pipeline works in isolation from input.
### 4
Wire input straight to output (no mapping yet)
Combine steps 2 and 3: read a physical event, forward the exact same event to a virtual device 1:1. No JSON, no LogicalControl enum, no classes beyond what's needed to keep the code readable. At this point you have a working (if dumb) passthrough controller — a real milestone.
### 5
Introduce ControllerProfile + LogicalControl
Now add the JSON device profile (physical BTN_SOUTH -> 'south', etc.) and the internal LogicalControl enum from the design. Hardcode a mapping for now (physical profile control -> LogicalControl -> Xbox virtual control) just to prove the translation layer works end to end.
### 6
Add user-editable Mapping + MappingEngine
Split the hardcoded mapping into its own JSON file and a real MappingEngine class, matching the ps4.json/xbox.json/user-mapping.json separation from your design. This is where remapping ('I want R1 to act as A') becomes possible without recompiling.
### 7
Build TerminalUI with command history
Wrap the working core in a simple command loop: list controllers, show mapping, set a mapping, save. Add CommandHistory for up/down arrow recall (readline or a hand-rolled vector + index works fine to start) and a basic Logger that the terminal can print to.
### 8
Add GuiUI on top of the same core
Once the terminal proves the underlying ApplicationServices layer is solid, build the GUI (ImGui or Qt) calling the exact same services — don't duplicate logic. This is also the point to formalize Application as the owner enforcing 'only one TerminalUI, only one GuiUI' via unique_ptr, as in the original design.