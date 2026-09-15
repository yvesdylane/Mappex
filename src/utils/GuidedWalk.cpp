#include "utils/GuidedWalk.h"
#include "controller/LogicalControl.h"
#include "utils/AppUtil.h"

#include <atomic>

namespace {

// Names a captured source for the binding log: "BTN_SOUTH", "ABS_HAT0Y (+)", ...
std::string sourceName(PhysicalController& controller, const PhysicalSource& src) {
    std::string name = controller.codeName(sourceEventType(src.kind), src.code);
    if (src.kind == SourceKind::Hat) name += (src.dir > 0 ? " (+)" : " (-)");
    return name;
}

}  // namespace

GuidedWalkReport runGuidedWalk(PhysicalController& controller,
                               Mapping& mapping,
                               const GuidedWalkOptions& opts) {
    const int skipMs = opts.skipTimeoutMs > 0 ? opts.skipTimeoutMs : 5000;
    std::atomic<bool> localStop{false};   // fallback when no stop flag is provided
    const std::atomic<bool>& stop = opts.stopFlag ? *opts.stopFlag : localStop;

    mapping.clear();

    GuidedWalkReport rep;
    auto stopped = [&stop] { return stop.load(); };

    // A step binds whatever the player actually pressed/pulled to the step's
    // logical target. Because pads expose the same input under different kinds
    // (BTN_SOUTH vs ABS_HAT0Y vs analog), any captured source is accepted.
    for (const auto& [name, lc] : guidedButtonList()) {
        if (stopped()) { rep.aborted = true; break; }
        if (opts.onStep) opts.onStep(name, GuidedStepKind::Button);
        CaptureResult r = captureWithAbort(controller, skipMs, stop);
        if (r.kind == CaptureKind::None) {
            if (stopped()) { rep.aborted = true; break; }
            rep.skipped++;
            continue;
        }
        PhysicalSource src = sourceFromCapture(r);
        mapping.addMapping(src, lc);
        if (opts.onBinding) opts.onBinding(sourceName(controller, src) + " -> " + name);
        rep.mapped++;
    }
    if (rep.aborted) return rep;

    for (const auto& [name, la] : guidedTriggerList()) {
        if (stopped()) { rep.aborted = true; break; }
        if (opts.onStep) opts.onStep(name, GuidedStepKind::Trigger);
        CaptureResult r = captureWithAbort(controller, skipMs, stop);
        if (r.kind == CaptureKind::None) {
            if (stopped()) { rep.aborted = true; break; }
            rep.skipped++;
            continue;
        }
        PhysicalSource src = sourceFromCapture(r);
        mapping.addMapping(src, la);
        if (opts.onBinding) opts.onBinding(sourceName(controller, src) + " -> " + name);
        rep.mapped++;
    }
    if (rep.aborted) return rep;

    for (const auto& [name, la] : guidedStickList()) {
        if (stopped()) { rep.aborted = true; break; }
        if (opts.onStep) opts.onStep(name, GuidedStepKind::Stick);
        CaptureResult r = captureWithAbort(controller, skipMs, stop);
        if (r.kind == CaptureKind::None) {
            if (stopped()) { rep.aborted = true; break; }
            rep.skipped++;
            continue;
        }
        PhysicalSource src = sourceFromCapture(r);
        mapping.addMapping(src, la);
        if (opts.onBinding) opts.onBinding(sourceName(controller, src) + " -> " + name);
        rep.mapped++;
    }
    if (rep.aborted) return rep;

    // D-pad: one physical direction binds one button (DPadLeft/Right/Up/Down).
    for (const auto& [name, lc] : guidedDPadList()) {
        if (stopped()) { rep.aborted = true; break; }
        if (opts.onStep) opts.onStep(name, GuidedStepKind::Dpad);
        CaptureResult r = captureWithAbort(controller, skipMs, stop);
        if (r.kind == CaptureKind::None) {
            if (stopped()) { rep.aborted = true; break; }
            rep.skipped++;
            continue;
        }
        PhysicalSource src = sourceFromCapture(r);
        mapping.addMapping(src, lc);
        if (opts.onBinding) opts.onBinding(sourceName(controller, src) + " -> " + name);
        rep.mapped++;
    }

    if (stopped()) rep.aborted = true;
    return rep;
}