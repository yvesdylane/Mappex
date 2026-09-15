#include "utils/AppUtil.h"
#include "controller/DeviceProfile.h"
#include <unistd.h>
#include <limits.h>
#include <cctype>
#include <cstdlib>
#include <vector>

namespace {

std::string executableDir() {
    char exe[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len < 0) return "";
    exe[len] = '\0';

    std::string path(exe);
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? "" : path.substr(0, slash);
}

// dirs to look in, newest first: cwd, exe dir, parent of exe dir.
std::vector<std::string> searchDirs(const std::string& exeDir) {
    std::vector<std::string> dirs;
    dirs.push_back(".");
    if (!exeDir.empty()) {
        dirs.push_back(exeDir);
        size_t parent = exeDir.find_last_of('/');
        if (parent != std::string::npos && parent > 0) {
            dirs.push_back(exeDir.substr(0, parent));
        }
    }
    return dirs;
}

// Where a new profile is written: project root when the exe sits in a build dir,
// otherwise the current directory.
std::string writeBase(const std::string& exeDir) {
    std::string base = exeDir.empty() ? "." : exeDir;
    size_t parent = base.find_last_of('/');
    if (parent != std::string::npos && parent > 0) base = base.substr(0, parent);
    return base;
}

// User-owned data dir for device profiles: $XDG_DATA_HOME/mappex (default
// ~/.local/share/mappex). Installed under /opt the app is read-only, so profiles
// live here instead of next to the binary.
std::string userDataDir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    std::string base = xdg && *xdg ? xdg : std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/.local/share";
    if (base.empty()) base = ".";
    return base + "/mappex";
}

}  // namespace

std::string findDeviceProfilePath(const DeviceIdentity& id) {
    std::string exeDir = executableDir();
    std::string relative = DeviceProfile::pathFor(id);
    std::string userPath = userDataDir() + "/" + relative;

    // Read order: cwd (dev), then the user's own data dir, then the bundled
    // locations next to the binary (project root in dev, /opt/Mappex when
    // installed). User-edited profiles always win over bundled ones.
    for (const auto& dir : searchDirs(exeDir)) {
        std::string candidate = dir + "/" + relative;
        if (access(candidate.c_str(), R_OK) == 0) return candidate;
    }
    if (access(userPath.c_str(), R_OK) == 0) return userPath;

    // Write destination: the location next to the binary when it is writable
    // (dev checkout), otherwise the user's data dir (system install).
    std::string base = writeBase(exeDir);
    if (access(base.c_str(), W_OK) == 0) return base + "/" + relative;
    return userPath;
}

int parseId(const std::string& s) {
    if (s.empty()) return -1;
    for (char c : s) {
        if (!std::isdigit((unsigned char)c)) return -1;
    }
    return std::atoi(s.c_str());
}

CaptureResult captureWithAbort(PhysicalController& controller, int timeoutMs,
                               const std::atomic<bool>& stopRequested) {
    const int CHUNK_MS = 200;
    for (int elapsed = 0; elapsed < timeoutMs; elapsed += CHUNK_MS) {
        if (stopRequested.load()) return CaptureResult{};
        CaptureResult result = controller.captureNext(CHUNK_MS);
        if (result.kind != CaptureKind::None) return result;
    }
    return CaptureResult{};
}