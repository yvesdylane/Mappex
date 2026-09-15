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

}  // namespace

std::string findDeviceProfilePath(const DeviceIdentity& id) {
    std::string exeDir = executableDir();
    std::string relative = DeviceProfile::pathFor(id);

    for (const auto& dir : searchDirs(exeDir)) {
        std::string candidate = dir + "/" + relative;
        if (access(candidate.c_str(), R_OK) == 0) return candidate;
    }

    return writeBase(exeDir) + "/" + relative;
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