#include "ui/TerminalUI.h"
#include "Logger.h"
#include <readline/readline.h>
#include <readline/history.h>
#include <sstream>
#include <cstring>

void TerminalUI::registerCommand(const std::string& name, const std::string& helpText, CommandHandler handler) {
    commands[name] = { helpText, std::move(handler) };
}

void TerminalUI::dispatch(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);

    if (tokens.empty()) return;

    std::string name = tokens.front();
    std::vector<std::string> args(tokens.begin() + 1, tokens.end());

    if (name == "quit" || name == "exit") {
        shouldQuit = true;
        return;
    }

    if (name == "help") {
        for (auto& [cmdName, cmd] : commands) {
            Logger::info(cmdName + " - " + cmd.helpText);
        }
        Logger::info("quit - stop the application");
        return;
    }

    auto it = commands.find(name);
    if (it == commands.end()) {
        Logger::warning("Unknown command: " + name + " (type 'help' for a list)");
        return;
    }

    it->second.handler(args);
}

void TerminalUI::run() {
    while (!shouldQuit) {
        char* raw = readline("> ");
        if (!raw) {                       // Ctrl+D / EOF
            printf("\n");
            break;
        }

        std::string line(raw);
        if (!line.empty()) {
            HIST_ENTRY* last = history_get(history_length);
            // Skip adding when identical to the most recent entry, so recalling a
            // command and running it doesn't duplicate it (matches shell behavior).
            if (last == nullptr || strcmp(last->line, line.c_str()) != 0) {
                add_history(raw);  // this is your up-arrow recall, handled by readline
            }
        }
        free(raw);

        dispatch(line);
    }
}