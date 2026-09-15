#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include "controller/ControllerManager.h"
#include "controller/LogicalControl.h"
#include "ui/IUserInterface.h"

class GuiUI : public IUserInterface {
public:
    GuiUI(ControllerManager& controllerManager, const std::atomic<bool>& stopRequested);

    ~GuiUI() override;
    void run() override;   // GLFW event loop until the window closes or stopRequested

private:
    void draw();
    void drawMappingForm(ManagedController* mc);

    void startRecord(ManagedController* mc, LogicalControl target);
    void startRecordAxis(ManagedController* mc, LogicalAxis target);

    ControllerManager& controllerManager;
    const std::atomic<bool>& stopRequested;

    int selectedId = -1;

    // One-shot "Record…" capture. Armed from a combo row, applied on the next
    // captured event. recordActive gates re-entry; the worker clears it on exit.
    std::thread recordThread;
    std::atomic<bool> recordActive{false};
    std::atomic<bool> recordAbort{false};
    LogicalControl recordTarget = LogicalControl::Unmapped;   // set before spawn, read-only after
    LogicalAxis recordAxisTarget = LogicalAxis::Invalid;
    bool recordIsControl = true;
};