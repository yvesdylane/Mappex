#pragma once
#include <atomic>
#include <csignal>
#include <memory>
#include <string>
#include "controller/ControllerManager.h"
#include "input/UdevMonitor.h"
#include "ui/IUserInterface.h"
#include "ui/TerminalUI.h"

class Application {
public:
    // When startupDevicePath is given, only that device is attached (legacy
    // `Mapping <path>` mode); otherwise every detected gamepad is attached.
    explicit Application(const std::string& startupDevicePath = "");
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void runTerminal();   // builds a TerminalUI, makes it the active UI, runs it
    void runGui();        // same pattern, for the ImGui backend

    void requestStop();

private:
    void setUI(std::unique_ptr<IUserInterface> ui);   // destroys whichever UI was running
    void registerCommands(TerminalUI& ui);
    void setupSignalHandling();
    void onDeviceEvent(const std::string& action, const std::string& node);
    void startGuidedWalk(ManagedController* mc);

    ControllerManager controllerManager;
    UdevMonitor udevMonitor;
    std::unique_ptr<IUserInterface> activeUI;   // never more than one alive at a time
    std::atomic<bool> stopRequested{false};
    sigset_t workerOldMask{};
    bool signalsBlocked = false;
};