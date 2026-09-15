cmake_minimum_required(VERSION 4.1)
project(Mapping)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ENV{PKG_CONFIG_PATH} "/usr/lib64/pkgconfig:/usr/share/pkgconfig:$ENV{PKG_CONFIG_PATH}")
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(LIBEVDEV REQUIRED libevdev)
pkg_check_modules(NLOHMANN REQUIRED nlohmann_json)
pkg_check_modules(READLINE REQUIRED readline)
pkg_check_modules(UDEV REQUIRED libudev)

add_executable(Mapping
        src/main.cpp
        src/Application.cpp
        src/Logger.cpp
        src/utils/AppUtil.cpp
        src/utils/GuidedWalk.cpp
        src/ui/TerminalUI.cpp
        src/input/EvdevEventReader.cpp
        src/input/ControllerDetector.cpp
        src/input/UdevMonitor.cpp
        src/controller/Mapping.cpp
        src/controller/DeviceProfile.cpp
        src/controller/PhysicalController.cpp
        src/controller/ControllerManager.cpp
        src/input/PortResolver.cpp
        src/virtual/XboxVirtualController.cpp
        src/virtual/UInputBackend.cpp
)

target_include_directories(Mapping PRIVATE include ${LIBEVDEV_INCLUDE_DIRS} ${NLOHMANN_INCLUDE_DIRS} ${READLINE_INCLUDE_DIRS} ${UDEV_INCLUDE_DIRS})
target_link_directories(Mapping PRIVATE ${LIBEVDEV_LIBRARY_DIRS} ${READLINE_LIBRARY_DIRS} ${UDEV_LIBRARY_DIRS})
target_link_libraries(Mapping PRIVATE ${LIBEVDEV_LIBRARIES} ${READLINE_LIBRARIES} ${UDEV_LIBRARIES} Threads::Threads)

# ---- GUI (ImGui + GLFW): only when glfw-devel is installed ----

find_package(glfw3 QUIET)
if(glfw3_FOUND)
    find_package(OpenGL REQUIRED)
    set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)

    add_library(imgui STATIC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

    target_compile_definitions(Mapping PRIVATE GUI_ENABLED)
    target_sources(Mapping PRIVATE src/ui/GuiUI.cpp)
    target_link_libraries(Mapping PRIVATE imgui)
else()
    message(STATUS "glfw3 not found — GUI disabled. Install with: sudo dnf install glfw-devel mesa-libGL-devel")
endif()

# ---- standalone test executables (each is self-contained, mirrors the old per-test build) ----

function(add_standalone_test target_name)
    add_executable(${target_name} tests/${target_name}.cpp)
    target_include_directories(${target_name} PRIVATE include ${LIBEVDEV_INCLUDE_DIRS} ${NLOHMANN_INCLUDE_DIRS})
    target_link_directories(${target_name} PRIVATE ${LIBEVDEV_LIBRARY_DIRS})
    target_link_libraries(${target_name} PRIVATE ${LIBEVDEV_LIBRARIES})
endfunction()

add_standalone_test(eventReaderTest)
add_standalone_test(uinputTest)
add_standalone_test(passthroughTest)
add_standalone_test(mappedPassthroughTest)
add_standalone_test(mappedPassthroughJsonTest)

# hardware-free unit test: Mapping parsing + XboxVirtualController code mapping
add_executable(mappingUnitTest
        tests/mappingUnitTest.cpp
        src/controller/Mapping.cpp
        src/virtual/XboxVirtualController.cpp
        src/Logger.cpp
)
target_include_directories(mappingUnitTest PRIVATE include ${NLOHMANN_INCLUDE_DIRS})

# controller detection test: creates a synthetic uinput gamepad, detector must find it
add_executable(controllerDetectorTest
        tests/controllerDetectorTest.cpp
        src/input/ControllerDetector.cpp
        src/Logger.cpp
)
target_include_directories(controllerDetectorTest PRIVATE include ${LIBEVDEV_INCLUDE_DIRS})
target_link_directories(controllerDetectorTest PRIVATE ${LIBEVDEV_LIBRARY_DIRS})
target_link_libraries(controllerDetectorTest PRIVATE ${LIBEVDEV_LIBRARIES})

# reader-thread stop test: run() joins cleanly after stop() with a fake reader
add_executable(physicalControllerTest
        tests/physicalControllerTest.cpp
        src/controller/PhysicalController.cpp
        src/controller/Mapping.cpp
        src/Logger.cpp
)
target_include_directories(physicalControllerTest PRIVATE include ${NLOHMANN_INCLUDE_DIRS})
target_link_libraries(physicalControllerTest PRIVATE ${LIBEVDEV_LIBRARIES})

# guided-walk engine test: fresh-layout walk over a scripted reader (buttons, triggers, sticks, abort)
add_executable(guidedWalkTest
        tests/guidedWalkTest.cpp
        src/utils/GuidedWalk.cpp
        src/utils/AppUtil.cpp
        src/controller/DeviceProfile.cpp
        src/controller/PhysicalController.cpp
        src/controller/Mapping.cpp
        src/Logger.cpp
)
target_include_directories(guidedWalkTest PRIVATE include ${NLOHMANN_INCLUDE_DIRS})
target_link_libraries(guidedWalkTest PRIVATE ${LIBEVDEV_LIBRARIES} Threads::Threads)

# controller manager test: attach/detach of synthetic uinput gamepads
add_executable(controllerManagerTest
        tests/controllerManagerTest.cpp
        src/controller/ControllerManager.cpp
        src/controller/DeviceProfile.cpp
        src/controller/PhysicalController.cpp
        src/controller/Mapping.cpp
        src/input/EvdevEventReader.cpp
        src/input/PortResolver.cpp
        src/utils/AppUtil.cpp
        src/virtual/UInputBackend.cpp
        src/virtual/XboxVirtualController.cpp
        src/Logger.cpp
)
target_include_directories(controllerManagerTest PRIVATE include ${LIBEVDEV_INCLUDE_DIRS} ${NLOHMANN_INCLUDE_DIRS})
target_link_directories(controllerManagerTest PRIVATE ${LIBEVDEV_LIBRARY_DIRS})
target_link_libraries(controllerManagerTest PRIVATE ${LIBEVDEV_LIBRARIES} Threads::Threads)

# per-device profile test: key derivation + JSON round trip incl. nested mapping
add_executable(deviceProfileTest
        tests/deviceProfileTest.cpp
        src/controller/DeviceProfile.cpp
        src/controller/Mapping.cpp
        src/Logger.cpp
)
target_include_directories(deviceProfileTest PRIVATE include ${NLOHMANN_INCLUDE_DIRS})

# udev monitor test: add/remove callbacks for a synthetic uinput pad
add_executable(udevMonitorTest
        tests/udevMonitorTest.cpp
        src/input/UdevMonitor.cpp
        src/Logger.cpp
)
target_include_directories(udevMonitorTest PRIVATE include ${UDEV_INCLUDE_DIRS})
target_link_directories(udevMonitorTest PRIVATE ${UDEV_LIBRARY_DIRS})
target_link_libraries(udevMonitorTest PRIVATE ${UDEV_LIBRARIES} Threads::Threads)

enable_testing()
add_test(NAME mappingUnitTest COMMAND mappingUnitTest)
add_test(NAME controllerDetectorTest COMMAND controllerDetectorTest)
add_test(NAME physicalControllerTest COMMAND physicalControllerTest)
add_test(NAME controllerManagerTest COMMAND controllerManagerTest)
add_test(NAME deviceProfileTest COMMAND deviceProfileTest)
add_test(NAME udevMonitorTest COMMAND udevMonitorTest)
add_test(NAME guidedWalkTest COMMAND guidedWalkTest)