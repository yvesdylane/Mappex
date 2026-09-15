#pragma once
#include <memory>
#include "virtual/VirtualController.h"
#include "virtual/OutputBackend.h"

class XboxVirtualController : public VirtualController {
public:
    explicit XboxVirtualController(std::unique_ptr<OutputBackend> backend);

    bool create() override;
    bool create(const VirtualDeviceInfo& info) override;
    void reset() override;
    std::string outputDevicePath() const override;
    int pressButton(LogicalControl control, int value) override;
    void setAxis(LogicalAxis axis, int value) override;

    static int buttonCode(LogicalControl control);
    static int axisCode(LogicalAxis axis);

private:
    void registerCapabilities();
    std::unique_ptr<OutputBackend> backend;
};
