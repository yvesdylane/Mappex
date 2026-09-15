#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include "Application.h"

int main(int argc, char** argv) {
    bool useGui = false;
    bool modeSet = false;
    std::string startupDevicePath;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--gui") == 0) {
            useGui = true;
            modeSet = true;
        } else if (std::strcmp(argv[i], "--terminal") == 0) {
            modeSet = true;
        } else if (std::strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
            startupDevicePath = argv[++i];
        } else {
            startupDevicePath = argv[i];   // bare device path (legacy single-controller mode)
        }
    }

    if (!modeSet) {
        if (isatty(STDIN_FILENO)) {
            printf("Run with (t)erminal or (g)ui? [t/g]: ");
            char choice = 0;
            if (scanf(" %c", &choice) == 1 && (choice == 'g' || choice == 'G')) {
                useGui = true;
            }
        }
        // stdin is not a tty (scripts): keep the terminal UI.
    }

    Application app(startupDevicePath);
    if (useGui) {
        app.runGui();
    } else {
        app.runTerminal();
    }
    return 0;
}