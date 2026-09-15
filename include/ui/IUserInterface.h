#pragma once

// Abstract UI backend. Only one implementation is alive at any time — Application
// owns a single instance and swaps it out via setUI(), which destroys whichever
// UI was running before.
class IUserInterface {
public:
    virtual ~IUserInterface() = default;
    virtual void run() = 0;   // blocks until the UI decides to quit
};