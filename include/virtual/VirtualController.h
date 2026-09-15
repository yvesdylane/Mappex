#pragma once
#include "controller/LogicalControl.h"
#include "virtual/OutputBackend.h"

class VirtualController {
public:
    virtual ~VirtualController() = default;

    // Identity-aware creation. Default delegates to the no-arg create().
    virtual bool create() = 0;
    virtual bool create(const VirtualDeviceInfo&) { return create(); }

    // Park the virtual pad: release every button/axis so nothing stays pressed
    // while the mapping is off. Default no-op.
    virtual void reset() {}

    // The /dev/input/eventN node this virtual device appears as, once created
    // ("" before then). Lets the caller skip its own outputs on hot-plug.
    virtual std::string outputDevicePath() const { return {}; }

    virtual int pressButton(LogicalControl control, int value) = 0;
    virtual void setAxis(LogicalAxis axis, int value) = 0;
};
