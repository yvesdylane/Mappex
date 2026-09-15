#include "Application.h"
#include "Logger.h"
#include "input/ControllerDetector.h"
#include "controller/DeviceProfile.h"
#include "controller/Mapping.h"
#include "controller/PhysicalController.h"
#include "utils/AppUtil.h"
#include "utils/GuidedWalk.h"

#include <csignal>
#include <linux/input.h>
#include <pthread.h>
#include <unistd.h>
#include <vector>

#ifdef GUI_ENABLED
#include "ui/GuiUI.h"
#endif

namespace {

Application* g_instance = nullptr;   // signal handlers are C callbacks — no way to pass 'this'

void handleSignal(int) {
    if (g_instance) g_instance->requestStop();
}

// Call BEFORE creating worker threads (readers, monitor); they inherit this
// mask at creation so only this (main) thread handles Ctrl+C.
sigset_t blockSignalsForWorkerThreads() {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigset_t old;
    pthread_sigmask(SIG_BLOCK, &set, &old);
    return old;
}

void restoreSignalMask(const sigset_t& old) {
    pthread_sigmask(SIG_SETMASK, &old, nullptr);
}

}  // namespace

Application::Application(const std::string& startupDevicePath) {
    g_instance = this;
    setupSignalHandling();

    workerOldMask = blockSignalsForWorkerThreads();
    signalsBlocked = true;

    if (startupDevicePath.empty()) {
        auto gamepads = ControllerDetector().findGamepads();
        for (const auto& pad : gamepads) {
            Logger::info("Detected controller: " + pad.path + " (" + pad.name + ")");
            controllerManager.attach(pad.path, nullptr);   // attach() builds the profile from device identity
        }
    } else {
        std::string name;
        if (!ControllerDetector::isGamepadDevice(startupDevicePath, &name)) {
            Logger::error("Not a gamepad: " + startupDevicePath);
        } else {
            Logger::info("Using controller: " + startupDevicePath + " (" + name + ")");
            controllerManager.attach(startupDevicePath, nullptr);
        }
    }

    if (controllerManager.ids().empty()) {
        Logger::warning("No controllers attached — hot-plug one in and 'list' will show it.");
    }
    Logger::info("Virtual devices created. Ctrl+C aborts a mapping walk.");

    udevMonitor.start([this](const std::string& action, const std::string& node) {
        onDeviceEvent(action, node);
    });
}

Application::~Application() {
    udevMonitor.stop();
    controllerManager.detachAll();
    g_instance = nullptr;
}

void Application::setupSignalHandling() {
    struct sigaction sa{};
    sa.sa_handler = handleSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

void Application::requestStop() {
    stopRequested.store(true);
}

void Application::setUI(std::unique_ptr<IUserInterface> ui) {
    activeUI.reset();          // destroy whatever UI was running first (at most one, ever)
    activeUI = std::move(ui);
}

void Application::onDeviceEvent(const std::string& action, const std::string& node) {
    if (action == "add") {
        std::string name;
        bool isPad = false;
        for (int attempt = 0; attempt < 10 && !isPad; ++attempt) {
            isPad = ControllerDetector::isGamepadDevice(node, &name);
            if (!isPad) usleep(100000);
        }
        if (!isPad) return;                       // skips keyboards/mice
        if (controllerManager.isKnownVirtualPath(node)) return;   // our own virtual outputs
        if (controllerManager.findByPath(node) != -1) return;
        int id = controllerManager.attach(node, nullptr);
        if (id >= 0) {
            Logger::info("Hot-plug: attached controller #" + std::to_string(id) + ": " + name);
        }
    } else if (action == "remove") {
        int id = controllerManager.findByPath(node);
        if (id >= 0) {
            controllerManager.detach(id);
            Logger::info("Hot-plug: detached controller #" + std::to_string(id));
        }
    }
}

void Application::runTerminal() {
    if (signalsBlocked) {
        restoreSignalMask(workerOldMask);
        signalsBlocked = false;
    }

    auto ui = std::make_unique<TerminalUI>();
    registerCommands(*ui);

    TerminalUI* rawUi = ui.get();
    setUI(std::move(ui));   // activeUI now owns it — any previous UI would have been destroyed here
    rawUi->run();
}

void Application::runGui() {
    if (signalsBlocked) {
        restoreSignalMask(workerOldMask);
        signalsBlocked = false;
    }

#ifdef GUI_ENABLED
    auto ui = std::make_unique<GuiUI>(controllerManager, stopRequested);
    GuiUI* rawUi = ui.get();
    setUI(std::move(ui));   // destroys whichever UI was running (none yet — single instance guarantee)
    rawUi->run();
#else
    Logger::warning("GUI support is not built (glfw3 missing). 'sudo dnf install glfw-devel' then rebuild.");
#endif
}

void Application::registerCommands(TerminalUI& ui) {
    ui.registerCommand("list", "list attached controllers with their ids", [this](const std::vector<std::string>&) {
        auto ids = controllerManager.ids();
        if (ids.empty()) {
            Logger::info("No controllers attached.");
            return;
        }
        for (int id : ids) {
            if (auto* mc = controllerManager.getManaged(id)) {
                std::string active = mc->controller->isActive() ? "ACTIVE" : "inactive";
                Logger::info("[" + std::to_string(id) + "] " + mc->deviceName + " (" + mc->devicePath +
                             ") — " + active);
            }
        }
    });

    ui.registerCommand("status", "show controller status: status <id>", [this](const std::vector<std::string>& args) {
        if (args.size() < 1) {
            Logger::warning("Usage: status <id>");
            return;
        }
        int id = parseId(args[0]);
        auto* mc = controllerManager.getManaged(id);
        if (!mc) {
            Logger::error("No controller with id " + args[0]);
            return;
        }
        Logger::info("Controller: " + mc->deviceName + " (" + mc->devicePath + ")");
        Logger::info("Identity key: " + DeviceProfile::keyFor(mc->profile->identity));
        Logger::info("Profile: " + findDeviceProfilePath(mc->profile->identity));
        Logger::info(std::string("Mapping: ") + (mc->controller->isActive() ? "ACTIVE" : "inactive") +
                     ", autoMap: " + (mc->profile->autoMap ? "on" : "off"));
    });

    ui.registerCommand("load", "reload this device's saved profile: load <id>", [this](const std::vector<std::string>& args) {
        if (args.size() < 1) {
            Logger::warning("Usage: load <id>");
            return;
        }
        int id = parseId(args[0]);
        auto* mc = controllerManager.getManaged(id);
        if (!mc) {
            Logger::error("No controller with id " + args[0]);
            return;
        }
        std::string path = findDeviceProfilePath(mc->profile->identity);
        DeviceProfile loaded;
        if (!loaded.loadFromFile(path)) {
            Logger::error("No saved profile at: " + path);
            return;
        }
        mc->profile->mapping = loaded.mapping;
        mc->profile->autoMap = loaded.autoMap;
        mc->controller->mapping_ref() = loaded.mapping;
        Logger::info("Loaded profile: " + path);
    });

    ui.registerCommand("save", "save the current mapping to this device's profile: save <id>", [this](const std::vector<std::string>& args) {
        if (args.size() < 1) {
            Logger::warning("Usage: save <id>");
            return;
        }
        int id = parseId(args[0]);
        auto* mc = controllerManager.getManaged(id);
        if (!mc) {
            Logger::error("No controller with id " + args[0]);
            return;
        }
        mc->profile->mapping = mc->controller->mapping_ref();   // runtime edits live on the controller
        std::string path = findDeviceProfilePath(mc->profile->identity);
        if (mc->profile->saveToFile(path)) {
            Logger::info("Saved profile: " + path);
        }
    });

    ui.registerCommand("active", "toggle the exclusive grab and virtual output: active <id> on|off", [this](const std::vector<std::string>& args) {
        if (args.size() < 2 || (args[1] != "on" && args[1] != "off")) {
            Logger::warning("Usage: active <id> on|off");
            return;
        }
        int id = parseId(args[0]);
        auto* mc = controllerManager.getManaged(id);
        if (!mc) {
            Logger::error("No controller with id " + args[0]);
            return;
        }
        mc->controller->setActive(args[1] == "on");
    });

    // map <id> <control>: any physical press/pull captures (a button, hat direction,
    // or analog axis) and binds to the target — pads expose the same input under
    // different kinds, so nothing is kind-restricted.
    // map <id> -all: guided walk over the whole controller.
    ui.registerCommand("map", "map a physical control: map <id> <A|B|X|Y|LB|RB|Start|Back|LS|RS|LeftTrigger|RightTrigger|LeftStickX|LeftStickY|RightStickX|RightStickY|DPadUp|DPadDown|DPadLeft|DPadRight>; or guided walk: map <id> -all", [this](const std::vector<std::string>& args) {
        if (args.size() < 2) {
            Logger::warning("Usage: map <id> <control>  — or map <id> -all for a guided walk (type 'help' for names)");
            return;
        }
        int id = parseId(args[0]);
        auto* mc = controllerManager.getManaged(id);
        if (!mc) {
            Logger::error("No controller with id " + args[0]);
            return;
        }
        std::string name = args[1];

        if (name == "-all") {
            stopRequested.store(false);   // fresh walk — a previous Ctrl+C must not cancel the next one
            startGuidedWalk(mc);
            return;
        }

        stopRequested.store(false);   // a previous Ctrl+C must not cancel this capture

        LogicalControl lc = logicalControlFromName(name);
        LogicalAxis la = logicalAxisFromName(name);
        if (lc == LogicalControl::Unmapped && la == LogicalAxis::Invalid) {
            Logger::warning("Unknown control: " + name);
            return;
        }

        const int CAPTURE_TIMEOUT_MS = 10000;
        Logger::info("Press/pull the physical control for \"" + name + "\"...");
        CaptureResult result = captureWithAbort(*mc->controller, CAPTURE_TIMEOUT_MS, stopRequested);

        if (result.kind == CaptureKind::None) {
            Logger::info(stopRequested.load() ? "Capture aborted." : "Nothing pressed for \"" + name + "\" — skipping.");
            return;
        }

        PhysicalSource src = sourceFromCapture(result);
        std::string srcName = mc->controller->codeName(sourceEventType(src.kind), src.code);
        if (src.kind == SourceKind::Hat) srcName += (src.dir > 0 ? " (+)" : " (-)");

        if (lc != LogicalControl::Unmapped) {
            mc->controller->mapping_ref().addMapping(src, lc);
            Logger::info("Physical " + srcName + " -> " + name + ". Run 'save' to persist.");
            return;
        }

        mc->controller->mapping_ref().addMapping(src, la);
        std::string digital = (src.kind != SourceKind::Axis) ? " (digital as analog)" : "";
        Logger::info("Physical " + srcName + " -> " + name + digital + ". Run 'save' to persist.");
    });
}

void Application::startGuidedWalk(ManagedController* mc) {
    std::string profilePath = findDeviceProfilePath(mc->profile->identity);
    Logger::info("Starting guided walk for: " + mc->deviceName + " (" + profilePath + ")");
    Logger::info("Existing mappings in this profile will be replaced when you 'save'.");

    const int SKIP_TIMEOUT_MS = 5000;
    Logger::info("Press each control; wait ~" + std::to_string(SKIP_TIMEOUT_MS / 1000) +
                 "s to skip it. Ctrl+C aborts the walk.");

    GuidedWalkOptions opts;
    opts.skipTimeoutMs = SKIP_TIMEOUT_MS;
    opts.stopFlag = &stopRequested;
    opts.onStep = [](const std::string& name, GuidedStepKind kind) {
        const char* verb = (kind == GuidedStepKind::Stick) ? "Push the" : "Press/pull the";
        Logger::info(std::string(verb) + " physical control for \"" + name + "\"...");
    };
    opts.onBinding = [](const std::string& note) { Logger::info("Physical " + note); };

    GuidedWalkReport rep = runGuidedWalk(*mc->controller, mc->controller->mapping_ref(), opts);

    if (rep.aborted) {
        Logger::warning("Guided mapping aborted. Nothing was saved.");
        return;
    }
    Logger::info("Guided mapping finished: " + std::to_string(rep.mapped) + " mapped, " +
                 std::to_string(rep.skipped) + " skipped. Run 'save' to persist to " + profilePath);
}