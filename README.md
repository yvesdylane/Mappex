# 🎮 MAPPEX

> **TURN ANY CONTROLLER INTO AN XBOX PAD. YOUR WAY.**

Tired of that one game that refuses to recognise your PS pad? Sick of button layouts
that make no sense? **Mappex** is a Linux controller remapper that grabs your physical
controller, rewrites every button/trigger/stick to your exact spec, and feeds a clean
**Xbox 360 pad (045e:028e)** to your games. Steam, Lutris, emulators — they all think
you're holding a perfectly normal Xbox controller. Because now you are.

---

## 🕹️ What The Heck Is This?

Mappex reads **any** Linux gamepad (PS3/PS4, Xbox, generic USB, whatever the kernel
sees via `evdev`) and turns it into a **virtual Xbox 360 pad**. But *you* decide the
mapping — not the driver, not the game, not your luck.

- A PS3 button that's dead? Bind a working one to it.
- D-pad on the right side of the pad? Move it. Physically.
- Want the Share button to be a trigger? Go wild.
- Playing an Xbox-only game on your PS pad? **This is where you live now.**

Mappex saves a **profile per device**, so every pad keeps its own loadout. Plug in,
grab, play. 💥

### Features

- ✅ **Any physical controller → Xbox 360 virtual pad** (the most recognised pad on Linux)
- ✅ **Guide/Home button supported** (the PS button / Xbox logo — maps to `BTN_MODE`)
- ✅ **Two UIs**: a fast **terminal** UI and a full **GUI** (ImGui)
- ✅ **Guided mapping wizard** — `map -all` walks you through every button, GG easy
- ✅ **Per-device profiles** with auto-load on connect (hot-plug friendly)
- ✅ **Exclusive grab** (`EVIOCGRAB`) — no ghost inputs leaking to other apps
- ✅ **Double-mapping** — one button can do *two* things (hello, macros-ish)
- ✅ **Analog mastery** — sticks, triggers, and even analog→digital (threshold-flip) bindings
- ✅ **D-pad as real buttons** (`BTN_DPAD_*`), not janky hat axes

---

## ⚙️ How It Works (The Groceries)

```
┌──────────────┐   libevdev    ┌───────────────┐   your   ┌──────────────────────┐
│ YOUR GAMEPAD │ ───────────► │ MAPPEX ENGINE │ ───────► │ VIRTUAL XBOX 360 PAD  │
│  (physical)  │  EVIOCGRAB   │  (mapping)    │  uinput  │ 045e:028e "X-Box 360" │
└──────────────┘              └───────────────┘          └──────────────────────┘
       ▲                            │                              │
       │                        profiles                        ✓ games
       │                   devices/<key>.json                think it's real
       └── udev hot-plug: plug in → profile auto-loads ───────┘
```

1. **Read**: Mappex opens your pad through `libevdev` and takes it **exclusively**
   (`EVIOCGRAB`) — nobody else sees its raw input anymore.
2. **Translate**: every physical press/pull hits your mapping table
   (`physical source → logical control`) — buttons, triggers, sticks, D-pad directions.
3. **Emit**: Mappex writes the translated events to a **virtual uinput device** that
   identifies as a **Microsoft X-Box 360 pad**. Your games just... work.
4. **Remember**: profiles are keyed by **device identity** (vendor+product+serial or
   USB-port), so unplugging and re-plugging restores the exact loadout.

### The D-pad situation 🧊

Real Xbox 360 pads expose their D-pad as **four buttons** (`BTN_DPAD_UP/DOWN/LEFT/RIGHT`).
Mappex does the same — each physical D-pad direction binds to its matching button.
No weird half-press hat-axis nonsense.

---

## 🔩 Requirements

- **Linux** (this is a Linux tool. It *is* the Linux tool.)
- Kernel with the **`uinput`** module (`/dev/uinput` must exist)
- Your user needs access to `/dev/input/event*` and `/dev/uinput`
  → you're probably fine if you're in the **`input`** group (see [Permissions](#🔐-permissions))
- **Fedora/RHEL** are the primary target; any distro with `dnf` works

### Dependencies (build)

| Package | Purpose |
|---|---|
| `gcc-c++` | the compiler, obviously |
| `cmake` | the build system |
| `pkgconf` | finds the libs |
| `libevdev-devel` | reads your physical pad |
| `readline-devel` | the terminal UI |
| `systemd-devel` | `libudev` hot-plug detection |
| `nlohmann-json-devel` | profile files (JSON) |
| `glfw-devel` + `mesa-libGL-devel` | the **GUI** (optional but why not) |

---

## 🛠️ Build It

Prefers a clean install? **`./install.sh`** clones the repo, builds, installs to
`/opt/Mappex` and drops a desktop launcher that opens the GUI straight from your
app menu. Manual build, if that's your thing:

```bash
# Fedora / RHEL — one-command dependency unlock
sudo dnf install -y gcc-c++ cmake pkgconf libevdev-devel readline-devel \
    systemd-devel nlohmann-json-devel glfw-devel mesa-libGL-devel

# Build
cmake -B build
cmake --build build -j$(nproc)

# Run the tests (come on, you know you want to)
ctest --test-dir build --output-on-failure
```

Your binaries land in `build/`. The main one is **`build/Mappex`**.

> No glfw found? No problem — Mappex builds fine as terminal-only. Ask dnf:
> `sudo dnf install glfw-devel mesa-libGL-devel`

---

## 🚀 Quickstart (2 Minutes, GG)

### Shortcut route: the GUI

```bash
./build/Mappex --gui
```

1. Your pad shows up on the left. Select it.
2. For every logical control row, hit **`Record…`** and press the physical button.
3. Tick **`Virtual mapping active`** — the pad is now yours. 👑
4. Tick **`Auto-map on connect`** so Mappex remembers forever.
5. **`Save profile`**. Done. Launch your game.

### Terminal route

```bash
./build/Mappex --terminal
```

```
list              → find your controller's id (e.g. 0)
map 0 -all        → guided walk: press the physical control for each logical one
active 0 on       → exclusive grab + virtual pad on
save 0            → save your loadout to the profile
```

That's it. You just became the remapper.

---

## 💻 Terminal Guide (The Command Deck)

| Command | What it does |
|---|---|
| `list` | List attached controllers (`id`, name, state). Active = green. |
| `status <id>` | Identity key, profile path, mapping state, autoMap flag |
| `map <id> <control>` | Press/pull any physical control and bind it to a target |
| `map <id> -all` | **Guided walk** — Mappex walks you through every target |
| `save <id>` | Persist the current mapping to `devices/<key>.json` |
| `load <id>` | Reload the saved profile from disk |
| `active <id> on\|off` | Toggle the exclusive grab + virtual output |
| `help` | Friendly reminder of these exact rules |
| `quit` | Exit to the desktop dimension |

### Mappable controls (logical targets)

```
Buttons:   A B X Y  LB RB  Start Back Home  LS RS  DPadUp/Down/Left/Right
Triggers:  LeftTrigger  RightTrigger
Sticks:    LeftStickX/Y  RightStickX/Y
```

`Home` is your **PS button / Xbox logo** — it emits `BTN_MODE` (0x13a) on the virtual pad.

---

## 🎛️ GUI Guide (The Control Center)

Launch with `./build/Mappex --gui`.

- **Left panel** — your controllers. Green = actively mapped.
- **Right panel** — one dropdown per logical control:
  - `— none —` → leave it alone
  - `Record…` → press/pull the physical control to bind it
  - or pick directly from the physical device's buttons/hats/axes
- **`Virtual mapping active`** checkbox → exclusive grab + virtual pad on/off
- **`Auto-map on connect`** checkbox → next connect auto-grabs & restores the profile
- **`Save profile`** → writes `devices/<key>.json`
- **`Clear all`** → nuclear reset of the bindings

---

## 🧠 Mapping Concepts (Know Your Builds)

**One source, many targets (double-mapping).** One physical button can fire two
logical controls. Like a tiny macro. Live your best life.

**One target, one source (last-wins).** If you bind logical `A` to a new physical
button, the old binding for `A` is removed everywhere. No overlaps, no drama.

**Analog everywhere.** Stick → stick, trigger → trigger, and even
**analog → digital**: bind an analog axis to a button and it flips at ~half travel.

**D-pad = four directions.** Each physical hat direction maps to one `BTN_DPAD_*`.

**Home/Guide. ✓** `LogicalControl::Home` → `BTN_MODE`, registered on the virtual pad.

---

## 💾 Storage Model (Where Your Loadouts Live)

- Profile file: **`devices/<key>.json`** — one per physical pad
- Device key: `vendor_product[_serial-or-usbport]` (identical pads stay
  distinguishable by which USB port they sit in)
- Format:
  ```json
  {
    "deviceName": "Sony PLAYSTATION(R)3 Controller",
    "vendorId": "054c", "productId": "0268",
    "serial": "55:aa:96:f4:bd:b9",
    "virtualIdentity": { "name": "...", "vendorId": "045e", "productId": "028e", "version": "0x0110" },
    "autoMap": true,
    "mapping": {
      "buttons": { "BTN_SOUTH": ["A", "LeftTrigger"] },
      "axes":    { "ABS_X": ["LeftStickX"] },
      "hats":    { "ABS_HAT0X": { "-1": ["DPadLeft"], "1": ["DPadRight"] } }
    }
  }
  ```
- Profiles are **migrated** on load: old Xbox-spoof identity → `045e:028e`,
  and legacy D-pad-as-hat-axes → the four directional D-pad buttons. Backwards-compatible, always.

---

## 🔐 Permissions (Because Linux)

Mappex needs to touch real hardware. Give your user access:

```bash
sudo usermod -aG input "$USER"
# then log out and back in (or reboot for good measure)
```

Make sure `uinput` is loaded:

```bash
ls /dev/uinput          # should exist
# if not:
sudo modprobe uinput
echo 'uinput' | sudo tee /etc/modules-load.d/uinput.conf   # persist across reboots
```

---

## 🆘 Troubleshooting (The "Why Isn't It Working" Dept.)

| Symptom | Fix |
|---|---|
| `Failed to read controller` | Check group membership: `groups` must list `input`. Re-login. |
| `Failed to create virtual device` | `/dev/uinput` missing → `sudo modprobe uinput`. Not writable → add yourself to `input`. |
| Game sees the pad but nothing moves | You mapped it but forgot `active <id> on` / the `Virtual mapping active` checkbox. |
| Two identical pads mix up profiles | They key off the USB port, so keep them in the same ports. |
| Device not detected at all | `evtest` it — if `evtest` can't see it, Linux can't, and neither can Mappex. |
| Only one of my buttons works | That button is probably bound to multiple things or not saved — `save <id>` after mapping. |

---

## 🧩 Project Layout (For The Curious)

```
include/controller/   Mapping + DeviceProfile + PhysicalController  (the brain)
include/input/        evdev reader, detector, udev monitor, port resolver
include/virtual/      XboxVirtualController → UInputBackend          (the output)
include/ui/           TerminalUI + GuiUI (ImGui)                     (the faces)
src/utils/            GuidedWalk wizard + path helpers
devices/              per-device profiles (created at runtime)
mappings/             legacy mapping templates
tests/                standalone unit + integration tests
```

**Testing**: `ctest --test-dir build` runs everything. Hardware-free unit tests
cover mapping parsing, virtual-pad code translation, and the guided walk. There are
also hardware tests that spin up a synthetic uinput pad — plug in a real pad for the full experience.

---

## 🏁 Final Boss

```
Plug in pad → ./build/Mappex --gui → Record… → Save → Game
```

That's the whole game plan. **Mappex on. 👾**