#include "ui/GuiUI.h"
#include "Logger.h"
#include "controller/DeviceProfile.h"
#include "controller/LogicalControl.h"
#include "input/EventReader.h"
#include "utils/AppUtil.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#include <GL/gl.h>
#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace {

// One selectable physical source in a row's dropdown.
struct CapSource {
    PhysicalSource source;
    std::string label;
};

// The device's actual events: buttons, then hat directions (each ±1 direction as
// a separate source), then plain analog axes. One uniform list — any-to-any.
std::vector<CapSource> buildSourceList(ManagedController* mc) {
    std::vector<CapSource> out;
    auto buttons = mc->controller->eventReaderCapsButtons();
    auto axes = mc->controller->eventReaderCapsAxes();

    for (const auto& cap : buttons) {
        std::string label = cap.name.empty() ? "BTN_<" + std::to_string(cap.code) + ">" : cap.name;
        out.push_back({ buttonSource(cap.code), label });
    }
    for (const auto& cap : axes) {
        if (!isHatAxisCode(cap.code)) continue;
        std::string base = cap.name.empty() ? "HAT_<" + std::to_string(cap.code) + ">" : cap.name;
        auto dirLabel = [&](int dir) {
            if (!cap.name.empty() && cap.name.back() == 'X') return base + (dir < 0 ? " Left" : " Right");
            if (!cap.name.empty() && cap.name.back() == 'Y') return base + (dir < 0 ? " Up" : " Down");
            return base + (dir < 0 ? " neg" : " pos");
        };
        out.push_back({ hatSource(cap.code, -1), dirLabel(-1) });
        out.push_back({ hatSource(cap.code, +1), dirLabel(+1) });
    }
    for (const auto& cap : axes) {
        if (isHatAxisCode(cap.code)) continue;
        std::string label = cap.name.empty() ? "ABS_<" + std::to_string(cap.code) + ">" : cap.name;
        out.push_back({ axisSource(cap.code), label });
    }
    return out;
}

std::string sourceLabel(const std::vector<CapSource>& list, const PhysicalSource& src) {
    for (const auto& c : list) if (c.source == src) return c.label;
    return "<bound>";
}

}  // namespace

GuiUI::GuiUI(ControllerManager& controllerManager, const std::atomic<bool>& stopRequested)
    : controllerManager(controllerManager), stopRequested(stopRequested) {}

GuiUI::~GuiUI() {
    recordAbort = true;   // captureNext is polled in 300ms chunks — bounded join
    if (recordThread.joinable()) recordThread.join();
}

void GuiUI::startRecord(ManagedController* mc, LogicalControl target) {
    if (recordActive.exchange(true)) return;   // only one pending capture at a time
    if (recordThread.joinable()) recordThread.join();
    recordTarget = target;
    recordAxisTarget = LogicalAxis::Invalid;
    recordIsControl = true;
    recordAbort = false;
    recordThread = std::thread([this, mc, target] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!recordAbort.load() && std::chrono::steady_clock::now() < deadline) {
            CaptureResult r = mc->controller->captureNext(300);
            if (r.kind != CaptureKind::None) {
                mc->controller->mapping_ref().addMapping(sourceFromCapture(r), target);
                break;
            }
        }
        recordActive = false;
    });
}

void GuiUI::startRecordAxis(ManagedController* mc, LogicalAxis target) {
    if (recordActive.exchange(true)) return;
    if (recordThread.joinable()) recordThread.join();
    recordTarget = LogicalControl::Unmapped;
    recordAxisTarget = target;
    recordIsControl = false;
    recordAbort = false;
    recordThread = std::thread([this, mc, target] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!recordAbort.load() && std::chrono::steady_clock::now() < deadline) {
            CaptureResult r = mc->controller->captureNext(300);
            if (r.kind != CaptureKind::None) {
                mc->controller->mapping_ref().addMapping(sourceFromCapture(r), target);
                break;
            }
        }
        recordActive = false;
    });
}

void GuiUI::run() {
    const char* forcedPlatform = std::getenv("MAPPING_GLFW_PLATFORM");
    if (forcedPlatform && std::strcmp(forcedPlatform, "x11") == 0)
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    else if (forcedPlatform && std::strcmp(forcedPlatform, "wayland") == 0)
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);

    if (!glfwInit()) {
        Logger::error("Failed to initialize GLFW.");
        return;
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(900, 600, "Mapping — Controller Mapper", nullptr, nullptr);
    if (!window) {
        Logger::error("Failed to create GLFW window.");
        glfwTerminate();
        return;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    while (!glfwWindowShouldClose(window) && !stopRequested.load()) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        draw();

        ImGui::Render();
        int displayW, displayH;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
}

void GuiUI::draw() {
    if (stopRequested.load()) recordAbort = true;   // SIGINT/SIGTERM cancels a pending capture

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    ImGui::Begin("Mapping");

    ImGui::BeginChild("Controllers", ImVec2(280, -1), true);
    ImGui::Text("Controllers");
    ImGui::Separator();
    auto ids = controllerManager.ids();
    if (ids.empty()) {
        ImGui::TextDisabled("None attached — plug one in.");
    }
    for (int id : ids) {
        if (auto* mc = controllerManager.getManaged(id)) {
            char label[256];
            snprintf(label, sizeof(label), "[%d] %s%s", id, mc->deviceName.c_str(),
                     mc->controller->isActive() ? "  (mapped)" : "");
            if (mc->controller->isActive()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.80f, 0.35f, 1.0f));
                const bool clicked = ImGui::Selectable(label, selectedId == id);
                ImGui::PopStyleColor();
                if (clicked) selectedId = id;
            } else if (ImGui::Selectable(label, selectedId == id)) {
                selectedId = id;
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("Details", ImVec2(-1, -1), true);

    auto* mc = controllerManager.getManaged(selectedId);
    if (!mc) {
        ImGui::TextDisabled("Select a controller on the left.");
    } else {
        drawMappingForm(mc);
    }

    ImGui::EndChild();
    ImGui::End();
}

void GuiUI::drawMappingForm(ManagedController* mc) {
    ImGui::Text("Controller: %s", mc->deviceName.c_str());
    ImGui::Text("Device:    %s", mc->devicePath.c_str());
    ImGui::Text("Profile:   %s", findDeviceProfilePath(mc->profile->identity).c_str());

    ImGui::Separator();

    bool active = mc->controller->isActive();
    if (ImGui::Checkbox("Virtual mapping active", &active)) {
        mc->controller->setActive(active);   // grabs/releases the physical device exclusively
        if (active) Logger::info("GuiUI: mapping activated for " + mc->deviceName);
    }
    ImGui::SameLine();
    bool autoMap = mc->profile->autoMap;
    if (ImGui::Checkbox("Auto-map on connect", &autoMap)) {
        mc->profile->autoMap = autoMap;
        mc->profile->mapping = mc->controller->mapping_ref();
        std::string path = findDeviceProfilePath(mc->profile->identity);
        mc->profile->saveToFile(path);
        Logger::info("GuiUI: autoMap " + std::string(autoMap ? "on" : "off") + " for " + mc->deviceName);
    }

    ImGui::Separator();

    Mapping& mappingRef = mc->controller->mapping_ref();
    ImGui::Text("Bindings set: %d", (int)mappingRef.mappingCount());
    if (ImGui::Button("Save profile")) {
        mc->profile->mapping = mappingRef;   // runtime bindings live on the controller — copy up first
        std::string path = findDeviceProfilePath(mc->profile->identity);
        mc->profile->saveToFile(path);
        Logger::info("GuiUI: saved profile to " + path);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear all")) {
        mappingRef.clear();
    }

    ImGui::Separator();

    auto sources = buildSourceList(mc);
    if (sources.empty()) {
        ImGui::TextDisabled("This device reports no button/axis events to bind.");
        return;
    }

    auto previewFor = [&](const std::optional<PhysicalSource>& ps) {
        if (!ps) return std::string("— none —");
        return sourceLabel(sources, *ps);
    };

    const bool recording = recordActive.load();

    auto groupHeader = [&](const char* title) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.75f, 0.85f, 1.0f, 1.0f), "%s", title);
        ImGui::Separator();
    };

    auto controlRow = [&](const std::string& label, int tag, LogicalControl target) {
        const bool recHere = recording && recordIsControl && recordTarget == target;
        const std::string preview = recHere ? "Recording…" : previewFor(mappingRef.sourceFor(target));
        ImGui::Text("%s", label.c_str());
        ImGui::SameLine(170.0f);
        char id[48];
        snprintf(id, sizeof(id), "##bind_%d", tag);
        ImGui::SetNextItemWidth(280.0f);
        if (!ImGui::BeginCombo(id, preview.c_str())) return;
        const bool noneSelected = !recHere && !mappingRef.sourceFor(target);
        if (ImGui::Selectable("— none —", noneSelected)) mappingRef.clearTarget(target);
        if (ImGui::Selectable("Record…", false)) {
            startRecord(mc, target);
            ImGui::EndCombo();
            return;
        }
        for (const auto& s : sources) {
            const bool selected = !recHere && mappingRef.sourceFor(target) == s.source;
            if (ImGui::Selectable(s.label.c_str(), selected)) {
                mappingRef.addMapping(s.source, target);
            }
        }
        ImGui::EndCombo();
    };

    auto axisRow = [&](const std::string& label, int tag, LogicalAxis target) {
        const bool recHere = recording && !recordIsControl && recordAxisTarget == target;
        const std::string preview = recHere ? "Recording…" : previewFor(mappingRef.sourceFor(target));
        ImGui::Text("%s", label.c_str());
        ImGui::SameLine(170.0f);
        char id[48];
        snprintf(id, sizeof(id), "##bind_%d", tag);
        ImGui::SetNextItemWidth(280.0f);
        if (!ImGui::BeginCombo(id, preview.c_str())) return;
        const bool noneSelected = !recHere && !mappingRef.sourceFor(target);
        if (ImGui::Selectable("— none —", noneSelected)) mappingRef.clearTarget(target);
        if (ImGui::Selectable("Record…", false)) {
            startRecordAxis(mc, target);
            ImGui::EndCombo();
            return;
        }
        for (const auto& s : sources) {
            const bool selected = !recHere && mappingRef.sourceFor(target) == s.source;
            if (ImGui::Selectable(s.label.c_str(), selected)) {
                mappingRef.addMapping(s.source, target);
            }
        }
        ImGui::EndCombo();
    };

    groupHeader("Buttons");
    int tag = 0;
    for (const auto& [label, target] : guidedButtonList()) controlRow(label, tag++, target);

    groupHeader("Triggers");
    for (const auto& [label, target] : guidedTriggerList()) axisRow(label, tag++, target);

    groupHeader("Sticks");
    for (const auto& [label, target] : guidedStickList()) axisRow(label, tag++, target);

    groupHeader("D-Pad");
    for (const auto& [label, target] : guidedDPadList()) controlRow(label, tag++, target);
}