#include "input/PortResolver.h"
#include <dirent.h>
#include <unistd.h>
#include <climits>
#include <cstring>

std::string PortResolver::resolvePathKey(const std::string& devicePath) {
    char targetReal[PATH_MAX];
    if (!realpath(devicePath.c_str(), targetReal)) return "";

    const char* byPathDir = "/dev/input/by-path";
    DIR* dir = opendir(byPathDir);
    if (!dir) return "";

    std::string result;
    while (struct dirent* entry = readdir(dir)) {
        if (entry->d_name[0] == '.') continue;

        std::string linkPath = std::string(byPathDir) + "/" + entry->d_name;
        char linkReal[PATH_MAX];
        if (realpath(linkPath.c_str(), linkReal) && strcmp(linkReal, targetReal) == 0) {
            result = entry->d_name;
            break;
        }
    }
    closedir(dir);
    return result;
}