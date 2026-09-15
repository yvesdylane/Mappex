#include <cassert>
#include <cstdio>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <vector>
#include <linux/input.h>
#include "controller/PhysicalController.h"
#include "input/EventReader.h"
#include "virtual/VirtualController.h"
#include "testFixtures.h"

int main() {
    // Original idle test: run() joins cleanly after stop() with no events.
    {
        auto reader = std::make_unique<IdleEventReader>();
        auto vc = std::make_unique<CountingVirtualController>();
        Mapping mapping;

        PhysicalController controller(std::move(reader), std::move(vc), std::move(mapping));

        std::thread worker([&controller] { controller.run(); });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        controller.stop();
        worker.join();
    }

    // Button capture: reader sends BTN_TRIGGER after 100ms; captureNext returns it.
    {
        std::vector<RawEvent> sequence = {
            RawEvent{ EV_KEY, BTN_TRIGGER, 1 }
        };
        auto reader = std::make_unique<ScriptedEventReader>(std::move(sequence), 0.0f,
                                                           std::chrono::milliseconds(80));
        auto vc = std::make_unique<CountingVirtualController>();
        Mapping mapping;

        PhysicalController controller(std::move(reader), std::move(vc), std::move(mapping));

        std::thread worker([&controller] { controller.run(); });

        CaptureResult result = controller.captureNext(5000);
        assert(result.kind == CaptureKind::Button);
        assert(result.code == BTN_TRIGGER);

        controller.stop();
        worker.join();
    }

    // Axis capture: reader sends ABS_X with a strong value; captureNext returns Axis.
    {
        std::vector<RawEvent> sequence = {
            RawEvent{ EV_ABS, ABS_X, 255 }
        };
        auto reader = std::make_unique<ScriptedEventReader>(std::move(sequence), 0.8f,
                                                           std::chrono::milliseconds(80));
        auto vc = std::make_unique<CountingVirtualController>();
        Mapping mapping;

        PhysicalController controller(std::move(reader), std::move(vc), std::move(mapping));

        std::thread worker([&controller] { controller.run(); });

        CaptureResult result = controller.captureNext(5000);
        assert(result.kind == CaptureKind::Axis);
        assert(result.code == ABS_X);
        assert(result.value == 255);

        controller.stop();
        worker.join();
    }

    // Timeout: idle reader, captureNext returns None before stop().
    {
        auto reader = std::make_unique<IdleEventReader>();
        auto vc = std::make_unique<CountingVirtualController>();
        Mapping mapping;

        PhysicalController controller(std::move(reader), std::move(vc), std::move(mapping));

        std::thread worker([&controller] { controller.run(); });

        CaptureResult result = controller.captureNext(80);
        assert(result.kind == CaptureKind::None);

        controller.stop();
        worker.join();
    }

    // stop() while capturing: reader never fires, stop() wakes the CV.
    {
        auto reader = std::make_unique<IdleEventReader>();
        auto vc = std::make_unique<CountingVirtualController>();
        Mapping mapping;

        PhysicalController controller(std::move(reader), std::move(vc), std::move(mapping));

        std::thread worker([&controller] { controller.run(); });

        // kick stop in background
        std::thread killer([&controller] {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            controller.stop();
        });

        CaptureResult result = controller.captureNext(5000);
        assert(result.kind == CaptureKind::None);

        worker.join();
        killer.join();
    }

    // Value scaling: triggers land in 0..255, sticks in ±32767,
    // and a digital button mapped to a trigger outputs 255/0.
    {
        std::vector<RawEvent> sequence = {
            RawEvent{ EV_ABS, ABS_RZ, 200 },
            RawEvent{ EV_ABS, ABS_X, 30000 },
            RawEvent{ EV_KEY, BTN_TRIGGER, 1 },
            RawEvent{ EV_KEY, BTN_TRIGGER, 0 },
        };
        auto reader = std::make_unique<ScriptedEventReader>(std::move(sequence), 0.0f,
                                                           std::chrono::milliseconds(60));
        auto vc = std::make_unique<CountingVirtualController>();
        CountingVirtualController* rawVc = vc.get();
        Mapping mapping;
        mapping.addAxisMapping(ABS_RZ, LogicalAxis::LeftTrigger);   // physical 0..255 trigger
        mapping.addAxisMapping(ABS_X, LogicalAxis::LeftStickX);     // physical stick
        mapping.addButtonAsAxisMapping(BTN_TRIGGER, LogicalAxis::RightTrigger);

        PhysicalController controller(std::move(reader), std::move(vc), std::move(mapping));
        controller.setActive(true);   // dispatch requires the mapping to be active

        std::thread worker([&controller] { controller.run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        controller.stop();
        worker.join();

        assert(rawVc->axisUpdates.size() == 4);

        assert(rawVc->axisUpdates[0].first == LogicalAxis::LeftTrigger);   // normalizeTrigger: 200/255*255
        assert(rawVc->axisUpdates[0].second == 200);

        assert(rawVc->axisUpdates[1].first == LogicalAxis::LeftStickX);    // normalizeAxis: +1.0 * 32767
        assert(rawVc->axisUpdates[1].second == 32767);

        assert(rawVc->axisUpdates[2].first == LogicalAxis::RightTrigger);  // button press -> 255
        assert(rawVc->axisUpdates[2].second == 255);

        assert(rawVc->axisUpdates[3].first == LogicalAxis::RightTrigger);  // button release -> 0
        assert(rawVc->axisUpdates[3].second == 0);
    }

    // Held axis: a joystick keeps re-sending a deflected value every poll. Only the
    // first rising deflection may capture; the same held value arriving in a later
    // capture window must NOT re-capture.
    {
        std::vector<RawEvent> sequence = {
            RawEvent{ EV_ABS, ABS_X, 200 },   // first arrival while deflected -> rising edge
            RawEvent{ EV_ABS, ABS_X, 200 },   // pad polls, still held
            RawEvent{ EV_ABS, ABS_X, 200 },   // still held
        };
        auto reader = std::make_unique<ScriptedEventReader>(std::move(sequence), 0.0f,
                                                           std::chrono::milliseconds(150));
        auto vc = std::make_unique<CountingVirtualController>();
        Mapping mapping;

        PhysicalController controller(std::move(reader), std::move(vc), std::move(mapping));
        std::thread worker([&controller] { controller.run(); });

        CaptureResult first = controller.captureNext(2000);
        assert(first.kind == CaptureKind::Axis);
        assert(first.code == ABS_X);

        CaptureResult held = controller.captureNext(1500);   // re-sends arrive here while still held
        assert(held.kind == CaptureKind::None);

        CaptureResult again = controller.captureNext(200);
        assert(again.kind == CaptureKind::None);

        controller.stop();
        worker.join();
    }

    // Release-then-repress: after a capture the player lets the stick back to center
    // and pushes it again for the next prompt; that must register as a new control.
    {
        std::vector<RawEvent> sequence = {
            RawEvent{ EV_ABS, ABS_X, 200 },   // capture #1
            RawEvent{ EV_ABS, ABS_X, 200 },   // still held (ignored)
            RawEvent{ EV_ABS, ABS_X, 128 },   // back to center (release; ignored)
            RawEvent{ EV_ABS, ABS_X, 128 },   // sitting at center
            RawEvent{ EV_ABS, ABS_X, 200 },   // genuine fresh press -> capture #2
        };
        auto reader = std::make_unique<ScriptedEventReader>(std::move(sequence), 0.0f,
                                                           std::chrono::milliseconds(150));
        auto vc = std::make_unique<CountingVirtualController>();
        Mapping mapping;

        PhysicalController controller(std::move(reader), std::move(vc), std::move(mapping));
        std::thread worker([&controller] { controller.run(); });

        CaptureResult first = controller.captureNext(2000);
        assert(first.kind == CaptureKind::Axis);
        assert(first.code == ABS_X);

        CaptureResult fresh = controller.captureNext(2500);   // press again after releasing
        assert(fresh.kind == CaptureKind::Axis);
        assert(fresh.code == ABS_X);

        CaptureResult none = controller.captureNext(200);
        assert(none.kind == CaptureKind::None);

        controller.stop();
        worker.join();
    }

    // Release in the gap between prompts: the player lets the stick back to center
    // while no capture window is open, then presses again for the next prompt. The
    // release must reset the edge state, or the fresh press would look like the same
    // "still held" axis and be wrongly rejected.
    {
        std::vector<RawEvent> sequence = {
            RawEvent{ EV_ABS, ABS_X, 200 },   // capture #1 (first window)
            RawEvent{ EV_ABS, ABS_X, 128 },   // released to center — arrives between windows
            RawEvent{ EV_ABS, ABS_X, 200 },   // genuine fresh press -> capture #2
        };
        auto reader = std::make_unique<ScriptedEventReader>(std::move(sequence), 0.0f,
                                                           std::chrono::milliseconds(180));
        auto vc = std::make_unique<CountingVirtualController>();
        Mapping mapping;

        PhysicalController controller(std::move(reader), std::move(vc), std::move(mapping));
        std::thread worker([&controller] { controller.run(); });

        CaptureResult first = controller.captureNext(2000);
        assert(first.kind == CaptureKind::Axis);
        assert(first.code == ABS_X);

        std::this_thread::sleep_for(std::chrono::milliseconds(300));   // e2 (~360ms) lands in the gap; window 2 opens before e3 (~540ms)

        CaptureResult fresh = controller.captureNext(2000);
        assert(fresh.kind == CaptureKind::Axis);
        assert(fresh.code == ABS_X);

        controller.stop();
        worker.join();
    }

// Hat dispatch: a hat direction presses its per-direction button targets and
// drives analog targets at full scale; a 0 release lets everything go.
    {
        std::vector<RawEvent> sequence = {
            RawEvent{ EV_ABS, ABS_HAT0Y, -1 },
            RawEvent{ EV_ABS, ABS_HAT0Y, 0 },
            RawEvent{ EV_ABS, ABS_HAT0Y, 1 },
            RawEvent{ EV_ABS, ABS_HAT0Y, 0 },
        };
        auto reader = std::make_unique<ScriptedEventReader>(std::move(sequence), 0.0f,
                                                             std::chrono::milliseconds(60));
        auto vc = std::make_unique<CountingVirtualController>();
        CountingVirtualController* rawVc = vc.get();
        Mapping mapping;
        mapping.addMapping(hatSource(ABS_HAT0Y, -1), LogicalControl::DPadUp);
        mapping.addMapping(hatSource(ABS_HAT0Y, 1), LogicalControl::DPadDown);
        mapping.addMapping(hatSource(ABS_HAT0Y, -1), LogicalAxis::LeftTrigger);

        PhysicalController controller(std::move(reader), std::move(vc), std::move(mapping));
        controller.setActive(true);

        std::thread worker([&controller] { controller.run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        controller.stop();
        worker.join();

        // e0: dir -1 → DPadUp press, LeftTrigger 255
        // e1: dir 0  → DPadUp + DPadDown release, LeftTrigger 0
        // e2: dir +1 → DPadDown press
        // e3: dir 0  → DPadUp + DPadDown release, LeftTrigger 0
        assert(rawVc->buttonUpdates.size() == 6);
        assert(rawVc->buttonUpdates[0] == std::make_pair(LogicalControl::DPadUp, 1));
        assert(rawVc->buttonUpdates[1] == std::make_pair(LogicalControl::DPadUp, 0));
        assert(rawVc->buttonUpdates[2] == std::make_pair(LogicalControl::DPadDown, 0));
        assert(rawVc->buttonUpdates[3] == std::make_pair(LogicalControl::DPadDown, 1));
        assert(rawVc->buttonUpdates[4] == std::make_pair(LogicalControl::DPadUp, 0));
        assert(rawVc->buttonUpdates[5] == std::make_pair(LogicalControl::DPadDown, 0));

        assert(rawVc->axisUpdates.size() == 3);
        assert(rawVc->axisUpdates[0] == std::make_pair(LogicalAxis::LeftTrigger, 255));
        assert(rawVc->axisUpdates[1] == std::make_pair(LogicalAxis::LeftTrigger, 0));
        assert(rawVc->axisUpdates[2] == std::make_pair(LogicalAxis::LeftTrigger, 0));
    }

    // Analog -> digital: crossing a 0.5 threshold edge fires the control; going
    // back across releases it; a fresh cross fires again.
    {
        std::vector<RawEvent> sequence = {
            RawEvent{ EV_ABS, ABS_X, 128 },   // rest
            RawEvent{ EV_ABS, ABS_X, 255 },   // cross up  -> pressed
            RawEvent{ EV_ABS, ABS_X, 128 },   // cross down -> released
            RawEvent{ EV_ABS, ABS_X, 255 },   // cross up again
        };
        auto reader = std::make_unique<ScriptedEventReader>(std::move(sequence), 0.0f,
                                                            std::chrono::milliseconds(60));
        auto vc = std::make_unique<CountingVirtualController>();
        CountingVirtualController* rawVc = vc.get();
        Mapping mapping;
        mapping.addMapping(axisSource(ABS_X), LogicalControl::B);

        PhysicalController controller(std::move(reader), std::move(vc), std::move(mapping));
        controller.setActive(true);

        std::thread worker([&controller] { controller.run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        controller.stop();
        worker.join();

        assert(rawVc->buttonUpdates.size() == 3);
        assert(rawVc->buttonUpdates[0] == std::make_pair(LogicalControl::B, 1));
        assert(rawVc->buttonUpdates[1] == std::make_pair(LogicalControl::B, 0));
        assert(rawVc->buttonUpdates[2] == std::make_pair(LogicalControl::B, 1));
    }

    // A hat direction captures as an Axis result carrying its sign — that's how
    // the walk/GUI rebuild the exact PhysicalSource afterwards.
    {
        std::vector<RawEvent> sequence = {
            RawEvent{ EV_ABS, ABS_HAT0Y, -1 }
        };
        auto reader = std::make_unique<ScriptedEventReader>(std::move(sequence), 0.0f,
                                                            std::chrono::milliseconds(80));
        auto vc = std::make_unique<CountingVirtualController>();
        Mapping mapping;

        PhysicalController controller(std::move(reader), std::move(vc), std::move(mapping));
        std::thread worker([&controller] { controller.run(); });

        CaptureResult result = controller.captureNext(5000);
        assert(result.kind == CaptureKind::Axis);
        assert(result.code == ABS_HAT0Y);
        assert(result.value == -1);

        controller.stop();
        worker.join();
    }

    // Active gating: while inactive, physical events are read but never dispatched;
    // activating grabs the device and starts dispatching; deactivating releases the
    // grab and parks the virtual pad (reset()). Everything during a capture stays
    // available even when the mapping is off — that's how the mapping walk works.
    {
        std::vector<RawEvent> sequence = {
            RawEvent{ EV_KEY, BTN_TRIGGER, 1 },   // e0: inactive, suppressed
            RawEvent{ EV_KEY, BTN_TRIGGER, 0 },   // e1: inactive, suppressed
            RawEvent{ EV_KEY, BTN_TRIGGER, 1 },   // e2: active, dispatch
            RawEvent{ EV_KEY, BTN_TRIGGER, 0 },   // e3: active, dispatch
        };
        auto reader = std::make_unique<ScriptedEventReader>(std::move(sequence), 0.0f,
                                                            std::chrono::milliseconds(60));
        ScriptedEventReader* rawReader = reader.get();
        auto vc = std::make_unique<CountingVirtualController>();
        CountingVirtualController* rawVc = vc.get();
        Mapping mapping;
        mapping.addButtonMapping(BTN_TRIGGER, LogicalControl::A);

        PhysicalController controller(std::move(reader), std::move(vc), std::move(mapping));
        assert(!controller.isActive());

        std::thread worker([&controller] { controller.run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(130));   // e0, e1 land here
        assert(rawVc->buttonUpdates.empty() && "inactive controller must not dispatch");

        controller.setActive(true);
        assert(controller.isActive());
        std::this_thread::sleep_for(std::chrono::milliseconds(200));   // e2, e3 dispatch
        assert(rawVc->buttonUpdates.size() == 2);
        assert(rawVc->buttonUpdates[0] == std::make_pair(LogicalControl::A, 1));
        assert(rawVc->buttonUpdates[1] == std::make_pair(LogicalControl::A, 0));
        assert(rawReader->grabCalls.size() >= 1 && rawReader->grabCalls[0] == true);

        controller.setActive(false);
        assert(!controller.isActive());
        assert(rawVc->resetCalls == 1 && "deactivate parks the virtual pad");
        assert(rawReader->grabCalls.size() >= 2 && rawReader->grabCalls[1] == false);

        controller.stop();
        worker.join();
    }

    printf("physicalControllerTest: all assertions passed\n");
    return 0;
}
