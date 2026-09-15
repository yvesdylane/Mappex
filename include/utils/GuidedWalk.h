#pragma once
// Shared guided-walk engine. The terminal and the GUI both drive the same
// press-each-control sequence through this so the two frontends can't drift.
#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include "controller/PhysicalController.h"

enum class GuidedStepKind { Button, Trigger, Stick, Dpad };

struct GuidedWalkOptions {
    int skipTimeoutMs = 5000;                       // silent-wait before a step counts as skipped
    const std::atomic<bool>* stopFlag = nullptr;    // polled between steps and inside each capture
    std::function<void(const std::string& label, GuidedStepKind kind)> onStep;      // before each capture window
    std::function<void(const std::string& bindingNote)> onBinding;                  // after each successful capture
};

struct GuidedWalkReport {
    std::size_t mapped = 0;
    std::size_t skipped = 0;
    bool aborted = false;   // stopFlag became true mid-walk
};

// Runs the full guided walk over `controller`, writing bindings into `mapping`.
// Always starts from a cleared mapping. Returns the per-section totals.
GuidedWalkReport runGuidedWalk(PhysicalController& controller,
                               Mapping& mapping,
                               const GuidedWalkOptions& opts);