#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include "ui/IUserInterface.h"

using CommandHandler = std::function<void(const std::vector<std::string>& args)>;

class TerminalUI : public IUserInterface {
public:
    void registerCommand(const std::string& name, const std::string& helpText, CommandHandler handler);
    void run() override;  // blocks until "quit" is entered or EOF (Ctrl+D)

private:
    void dispatch(const std::string& line);

    struct Command {
        std::string helpText;
        CommandHandler handler;
    };

    std::unordered_map<std::string, Command> commands;
    bool shouldQuit = false;
};