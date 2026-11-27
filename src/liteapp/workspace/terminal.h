#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#include "liteapp/core/result.h"

namespace liteapp::workspace {

struct TerminalSize {
    int columns{80};
    int rows{24};
};

struct TerminalCallbacks {
    // Receives raw terminal output; returning false closes the terminal.
    std::function<bool(std::string_view)> output;
    // Polled about every 100 ms; returning false closes the terminal (e.g. its client disconnected).
    std::function<bool()> active;
    // Called once after the shell exits (-1 when it was killed).
    std::function<void(int exit_code)> exited;
};

// Interactive shells ($SHELL, else /bin/sh) on pseudo-terminals, started in one directory. POSIX only.
// Each terminal is read on its own thread; destruction hangs up every shell and joins the threads.
class Terminals {
public:
    explicit Terminals(std::filesystem::path directory);
    ~Terminals();
    Terminals(const Terminals&) = delete;
    Terminals& operator=(const Terminals&) = delete;

    // Starts a shell and returns its terminal ID. The shell gets an unblocked signal mask and TERM=xterm-256color.
    [[nodiscard]] Result<std::int64_t> open(TerminalSize size, TerminalCallbacks callbacks);
    [[nodiscard]] Result<void> write(std::int64_t id, std::string_view data);
    [[nodiscard]] Result<void> resize(std::int64_t id, TerminalSize size);
    // Hangs up every shell and waits for their reader threads.
    void close_all();

private:
    struct Pty;
    std::shared_ptr<Pty> find(std::int64_t id);

    std::filesystem::path directory_;
    std::mutex mutex_;
    std::map<std::int64_t, std::shared_ptr<Pty>> terminals_;
    std::vector<std::jthread> threads_;
    std::atomic<std::int64_t> next_id_{0};
    std::atomic<bool> stopping_{false};
};

}  // namespace liteapp::workspace
