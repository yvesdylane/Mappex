// Guided-walk engine tests. Covers the button/digital-as-axis and axis capture
// paths, skip-on-timeout, abort-on-stop-flag, and the preserves-mapping-wipe.
#include <atomic>
#include <cassert>
#include <cstdio>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <linux/input.h>
#include "controller/LogicalControl.h"
#include "controller/Mapping.h"
#include "controller/PhysicalController.h"
#include "utils/GuidedWalk.h"
#include "testFixtures.h"

namespace {
bool contains(const std::vector<LogicalControl>& v, LogicalControl c) {
    for (auto x : v) if (x == c) return true;
    return false;
}
bool notEmpty(const std::vector<LogicalAxis>& v) { return !v.empty(); }
bool notEmpty(const std::vector<LogicalControl>& v) { return !v.empty(); }
}  // namespace

constexpr std::size_t kTotalSteps = 20;   // 10 buttons + 2 triggers + 4 sticks + 4 D-pad directions

int main() {
    // (a) A single BTN_SOUTH press during the first (A) window:
    //     mapped=1, skipped=19, first announced step "A", binding written.
    {
        std::vector<RawEvent> seq = { RawEvent{ EV_KEY, BTN_SOUTH, 1 } };
        auto reader = std::make_unique<ScriptedEventReader>(std::move(seq), 0.0f,
                                                            std::chrono::milliseconds(60));
        auto vc = std::make_unique<CountingVirtualController>();
        Mapping mapping;
        PhysicalController controller(std::move(reader), std::move(vc), std::move(mapping));
        std::thread worker([&controller] { controller.run(); });

        std::vector<std::string> labels;
        std::vector<std::string> notes;
        GuidedWalkOptions opts;
        opts.skipTimeoutMs = 200;
        opts.onStep = [&](const std::string& label, GuidedStepKind) { labels.push_back(label); };
        opts.onBinding = [&](const std::string& note) { notes.push_back(note); };

        GuidedWalkReport rep = runGuidedWalk(controller, controller.mapping_ref(), opts);

        controller.stop();
        worker.join();

        assert(rep.mapped == 1);
        assert(rep.skipped == kTotalSteps - 1);
        assert(!rep.aborted);
        assert(!labels.empty() && labels[0] == "A");
        assert(notes.size() == 1 && notes[0].find("A") != std::string::npos);
        assert(contains(controller.mapping_ref().translateButton(BTN_SOUTH), LogicalControl::A));
        assert(controller.mapping_ref().mappingCount() == 1);
    }

    // (b) Idle pad: every step skips; the walk starts from a cleared mapping.
    {
        auto reader = std::make_unique<IdleEventReader>();
        auto vc = std::make_unique<CountingVirtualController>();
        Mapping mapping;
        mapping.addButtonMapping(BTN_TRIGGER, LogicalControl::B);   // must be wiped by the walk
        PhysicalController controller(std::move(reader), std::move(vc), std::move(mapping));
        std::thread worker([&controller] { controller.run(); });

        GuidedWalkOptions opts;
        opts.skipTimeoutMs = 200;
        GuidedWalkReport rep = runGuidedWalk(controller, controller.mapping_ref(), opts);

        controller.stop();
        worker.join();

        assert(rep.mapped == 0);
        assert(rep.skipped == kTotalSteps);
        assert(!rep.aborted);
        assert(controller.mapping_ref().mappingCount() == 0);
        assert(!contains(controller.mapping_ref().translateButton(BTN_TRIGGER), LogicalControl::B));
    }

    // (c) Button-only presses every ~10ms: every step — buttons, triggers AND
    //     sticks — binds from the single button source (any-to-any).
    {
        std::vector<RawEvent> seq;
        for (int i = 0; i < 600; ++i) {
            seq.push_back(RawEvent{ EV_KEY, BTN_SOUTH, 1 });
            seq.push_back(RawEvent{ EV_KEY, BTN_SOUTH, 0 });
        }
        auto reader = std::make_unique<ScriptedEventReader>(std::move(seq), 0.0f,
                                                            std::chrono::milliseconds(10));
        auto vc = std::make_unique<CountingVirtualController>();
        Mapping mapping;
        PhysicalController controller(std::move(reader), std::move(vc), std::move(mapping));
        std::thread worker([&controller] { controller.run(); });

        GuidedWalkOptions opts;
        opts.skipTimeoutMs = 200;
        GuidedWalkReport rep = runGuidedWalk(controller, controller.mapping_ref(), opts);

        controller.stop();
        worker.join();

        assert(rep.mapped == kTotalSteps);
        assert(rep.skipped == 0);
        assert(!rep.aborted);
        assert(notEmpty(controller.mapping_ref().translateButton(BTN_SOUTH)));         // buttons incl. D-pad
        assert(notEmpty(controller.mapping_ref().translateButtonAsAxis(BTN_SOUTH)));   // triggers + sticks
        assert(controller.mapping_ref().translateButtonAsAxis(BTN_SOUTH).size() == 6);  // 2 triggers + 4 sticks
        assert(contains(controller.mapping_ref().translateButton(BTN_SOUTH), LogicalControl::DPadUp));
        assert(contains(controller.mapping_ref().translateButton(BTN_SOUTH), LogicalControl::DPadDown));
        assert(contains(controller.mapping_ref().translateButton(BTN_SOUTH), LogicalControl::DPadLeft));
        assert(contains(controller.mapping_ref().translateButton(BTN_SOUTH), LogicalControl::DPadRight));
    }

    // (d) stopFlag already set: walk aborts before the first step, nothing walked.
    {
        auto reader = std::make_unique<IdleEventReader>();
        auto vc = std::make_unique<CountingVirtualController>();
        Mapping mapping;
        mapping.addButtonMapping(BTN_TRIGGER, LogicalControl::X);
        PhysicalController controller(std::move(reader), std::move(vc), std::move(mapping));
        std::thread worker([&controller] { controller.run(); });

        std::atomic<bool> stopFlag{true};
        std::vector<std::string> labels;
        GuidedWalkOptions opts;
        opts.skipTimeoutMs = 200;
        opts.stopFlag = &stopFlag;
        opts.onStep = [&](const std::string& label, GuidedStepKind) { labels.push_back(label); };

        GuidedWalkReport rep = runGuidedWalk(controller, controller.mapping_ref(), opts);

        controller.stop();
        worker.join();

        assert(rep.aborted);
        assert(rep.mapped == 0);
        assert(rep.skipped == 0);
        assert(labels.empty());
        assert(controller.mapping_ref().mappingCount() == 0);
    }

    // (e) Axis-only cycle: every step binds from the ABS_X analog source
    //     (sticks/triggers get the continuous value, buttons get the threshold switch).
    {
        std::vector<RawEvent> seq;
        for (int i = 0; i < 600; ++i) {
            seq.push_back(RawEvent{ EV_ABS, ABS_X, 255 });
            seq.push_back(RawEvent{ EV_ABS, ABS_X, 128 });
        }
        auto reader = std::make_unique<ScriptedEventReader>(std::move(seq), 0.0f,
                                                            std::chrono::milliseconds(10));
        auto vc = std::make_unique<CountingVirtualController>();
        Mapping mapping;
        PhysicalController controller(std::move(reader), std::move(vc), std::move(mapping));
        std::thread worker([&controller] { controller.run(); });

        GuidedWalkOptions opts;
        opts.skipTimeoutMs = 200;
        GuidedWalkReport rep = runGuidedWalk(controller, controller.mapping_ref(), opts);

        controller.stop();
        worker.join();

        assert(rep.mapped == kTotalSteps);
        assert(rep.skipped == 0);
        assert(!rep.aborted);
        assert(notEmpty(controller.mapping_ref().translateAxis(ABS_X)));
        assert(contains(controller.mapping_ref().controlTargets(axisSource(ABS_X)), LogicalControl::A));
    }

    // (f) Hat-direction capture: pressing ABS_HAT0Y down binds the (-1) direction.
    {
        std::vector<RawEvent> seq = { RawEvent{ EV_ABS, ABS_HAT0Y, -1 } };
        auto reader = std::make_unique<ScriptedEventReader>(std::move(seq), 0.0f,
                                                            std::chrono::milliseconds(60));
        auto vc = std::make_unique<CountingVirtualController>();
        Mapping mapping;
        PhysicalController controller(std::move(reader), std::move(vc), std::move(mapping));
        std::thread worker([&controller] { controller.run(); });

        GuidedWalkOptions opts;
        opts.skipTimeoutMs = 300;
        GuidedWalkReport rep = runGuidedWalk(controller, controller.mapping_ref(), opts);

        controller.stop();
        worker.join();

        assert(rep.mapped == 1);
        assert(rep.skipped == kTotalSteps - 1);
        assert(!rep.aborted);
        auto hatTargets = controller.mapping_ref().translateHat(ABS_HAT0Y, -1);
        assert(contains(hatTargets, LogicalControl::A));
    }

    std::printf("guidedWalkTest: all cases passed\n");
    return 0;
}