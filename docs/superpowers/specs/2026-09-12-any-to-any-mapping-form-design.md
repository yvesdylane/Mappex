# Any-to-Any Static Mapping Form (design)

Date: 2026-09-12

## Goal

Replace the guided-walk-as-primary GUI flow with a static form: one dropdown row
per logical control (14 buttons incl. D-pad, 2 triggers, 4 sticks). Every row can
be bound to **any** physical event kind — button, hat direction, or analog axis —
because different pads expose the same inputs under different kinds (`BTN_DPAD_*`
vs `ABS_HAT0X/0Y`, `BTN_TL/TR` vs analog `ABS_Z/RZ`, sticks as buttons vs axes).

The guided walk remains as the *terminal* power-user flow and is removed from the GUI.

## Data model

- `PhysicalSource { SourceKind kind (Button|Hat|Axis); int code; int dir; }`
  - `dir` meaningful only for hats (`-1`/+`1`)
- `Mapping` keeps two maps keyed by `PhysicalSource`:
  - `controlBySource: PhysicalSource -> vector<LogicalControl>`
  - `axisBySource: PhysicalSource -> vector<LogicalAxis>`
- Uniqueness rule (user-confirmed): a logical target may be driven by **one**
  physical source. `addMapping(src, target)` removes `target` from every other
  source first ("replace" semantics), so mapping e.g. `A` to a new source unbinds
  the old one. One physical source may still drive many logical targets (double
  mapping).
- Reverse lookup `sourceFor(LogicalControl|LogicalAxis) -> optional<PhysicalSource>`
  feeds the combo previews; `clearTarget(...)` unbinds a row ("— none —").
- JSON gains an additive `"hats"` block:
  `{"ABS_HAT0Y": {"-1": ["DPadUp"], "1": ["DPadDown"]}}`
  Load applies the same last-wins uniqueness per target.

## Runtime translation

| physical            | digital target             | analog target               |
|---------------------|----------------------------|-----------------------------|
| button              | press/release              | full-on 255/32767, else 0   |
| hat direction       | press/release              | full-on/0                   |
| analog axis         | 0.5-edge threshold switch* | normalized continuous value |

\* uses `min(centerDeflection, minDeflection) >= 0.5` — a resting trigger reads
high under the center model and a centered stick reads ~0.5 under the min model,
so taking the lower of the two avoids false presses at rest.

`CaptureResult` gains `int value` (raw axis value / hat direction) so captures can
produce the right `PhysicalSource` (`dir` from the hat sign).

## Capability listing

- `DeviceCapability { int code; std::string name; }`
- `EventReader` gains pure virtual `listButtonCapabilities()` /
  `listAxisCapabilities()`; `EvdevEventReader` iterates `KEY_MAX`/`ABS_MAX` with
  `libevdev_has_event_code`.
- `PhysicalController` exposes `eventReaderCapsButtons()` / `eventReaderCapsAxes()`.
- Hats (`ABS_HAT0X..ABS_HAT3Y`, via `isHatAxisCode`) are split into two directional
  entries and excluded from the plain axis list.

## GUI form

- Left: controller list (unchanged). Right: the form.
- Button group: 14 rows (A..D-pad). Trigger + stick groups: 6 rows.
- Every combo = `— none —`, `Record…`, then the device's buttons + hat
  directions + analog axes in one uniform list (any-to-any).
- `Record…` arms a one-shot 15 s background capture thread (chunked 300 ms so the
  app can exit promptly); on capture it binds the produced `PhysicalSource`.
- On top: `Save profile`, `Clear all`, bindings count.

## Walk / terminal

- `runGuidedWalk` drops the per-step kind gating: any captured source binds the
  current target. `map <id> <control>` in the terminal behaves the same.
- `sourceFromCapture(CaptureResult) -> PhysicalSource` helper in `utils/AppUtil.h`.

## Tests

- `mappingUnitTest`: replacement semantics, hat mapping, analog→digital binding,
  `sourceFor`/`clearTarget`, hats round-trip through save/load.
- `physicalControllerTest`: hat dispatch, any-to-any dispatch, analog→digital
  threshold flips, capture `value`.
- `guidedWalkTest`: kind-agnostic walk (buttons/triggers/sticks all capture from a
  single source), plus a hat-direction capture case.
- Manual GUI pass with the USB GamePad (cwd=/tmp/opencode; scratch save only).
- Minimal `request.txt` (CLI/GUI app, no HTTP endpoints).