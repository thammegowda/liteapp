#include "liteapp/workspace/terminal.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <string>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#define LITEAPP_TERMINAL_POSIX 1
#include "liteapp/core/posix.h"
extern char** environ;
#endif

namespace liteapp::workspace {

struct Terminals::Pty {
    int fd{-1};
    int pid{-1};
    std::mutex write_mutex;
};

#if defined(LITEAPP_TERMINAL_POSIX)
namespace {

winsize window(TerminalSize size) {
    return {.ws_row = static_cast<unsigned short>(std::clamp(size.rows, 2, 500)),
            .ws_col = static_cast<unsigned short>(std::clamp(size.columns, 2, 1000)),
            .ws_xpixel = 0,
            .ws_ypixel = 0};
}

}  // namespace
#endif

Terminals::Terminals(std::filesystem::path directory) : directory_(std::move(directory)) {}

Terminals::~Terminals() { close_all(); }

std::shared_ptr<Terminals::Pty> Terminals::find(std::int64_t id) {
    const std::lock_guard lock(mutex_);
    const auto found = terminals_.find(id);
    return found == terminals_.end() ? nullptr : found->second;
}

#if defined(LITEAPP_TERMINAL_POSIX)

Result<std::int64_t> Terminals::open(TerminalSize size, TerminalCallbacks callbacks) {
    if (stopping_) return std::unexpected(Error{"closed", "terminals are shutting down"});
    auto dimensions = window(size);
    // Everything the child needs is prepared before fork: only async-signal-safe calls may follow it.
    const char* shell = std::getenv("SHELL");
    const std::string program = shell && *shell && ::access(shell, X_OK) == 0 ? shell : "/bin/sh";
    std::vector<std::string> environment{"TERM=xterm-256color", "COLORTERM=truecolor"};
    for (char** entry = environ; *entry; ++entry) {
        const std::string_view value(*entry);
        if (!value.starts_with("TERM=") && !value.starts_with("COLORTERM=")) environment.emplace_back(value);
    }
    std::vector<char*> envp;
    for (auto& value : environment) envp.push_back(value.data());
    envp.push_back(nullptr);
    const std::string directory = directory_.string();
    char* argv[] = {const_cast<char*>(program.c_str()), nullptr};
    auto pty = std::make_shared<Pty>();
    pty->pid = ::forkpty(&pty->fd, nullptr, nullptr, &dimensions);
    if (pty->pid < 0) return std::unexpected(Error{"io", "cannot open a pseudo-terminal"});
    if (pty->pid == 0) {
        posix::reset_child_signals();
        if (::chdir(directory.c_str()) < 0) ::_exit(126);
        ::execve(program.c_str(), argv, envp.data());
        ::_exit(127);
    }
    ::fcntl(pty->fd, F_SETFD, FD_CLOEXEC);
    const auto id = ++next_id_;
    const std::lock_guard lock(mutex_);
    terminals_[id] = pty;
    threads_.emplace_back([this, id, pty, callbacks = std::move(callbacks)] {
        std::array<char, 16384> buffer{};
        while (!stopping_ && (!callbacks.active || callbacks.active())) {
            pollfd ready{.fd = pty->fd, .events = POLLIN, .revents = 0};
            const auto polled = ::poll(&ready, 1, 100);
            if (polled < 0 && errno != EINTR) break;
            if (polled <= 0) continue;
            const auto count = ::read(pty->fd, buffer.data(), buffer.size());
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            if (callbacks.output && !callbacks.output({buffer.data(), static_cast<std::size_t>(count)})) break;
        }
        ::kill(pty->pid, SIGHUP);
        int status = 0;
        for (int attempt = 0; attempt < 20 && ::waitpid(pty->pid, &status, WNOHANG) == 0; ++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (::waitpid(pty->pid, &status, WNOHANG) == 0) {
            ::kill(pty->pid, SIGKILL);
            ::waitpid(pty->pid, &status, 0);
        }
        {
            const std::lock_guard registry(mutex_);
            terminals_.erase(id);
        }
        {
            const std::lock_guard writer(pty->write_mutex);
            ::close(std::exchange(pty->fd, -1));
        }
        if (callbacks.exited) callbacks.exited(WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    });
    return id;
}

Result<void> Terminals::write(std::int64_t id, std::string_view data) {
    auto pty = find(id);
    if (!pty) return std::unexpected(Error{"closed", "terminal is closed"});
    const std::lock_guard lock(pty->write_mutex);
    if (pty->fd < 0) return std::unexpected(Error{"closed", "terminal is closed"});
    while (!data.empty()) {
        const auto count = ::write(pty->fd, data.data(), data.size());
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return std::unexpected(Error{"closed", "terminal is closed"});
        data.remove_prefix(static_cast<std::size_t>(count));
    }
    return {};
}

Result<void> Terminals::resize(std::int64_t id, TerminalSize size) {
    auto pty = find(id);
    if (!pty) return std::unexpected(Error{"closed", "terminal is closed"});
    auto dimensions = window(size);
    const std::lock_guard lock(pty->write_mutex);
    if (pty->fd >= 0) ::ioctl(pty->fd, TIOCSWINSZ, &dimensions);
    return {};
}

void Terminals::close_all() {
    {
        const std::lock_guard lock(mutex_);
        for (const auto& [id, pty] : terminals_) ::kill(pty->pid, SIGHUP);
    }
    stopping_ = true;
    std::vector<std::jthread> threads;
    {
        const std::lock_guard lock(mutex_);
        threads.swap(threads_);
    }
    threads.clear();
}

#else

Result<std::int64_t> Terminals::open(TerminalSize, TerminalCallbacks) {
    return std::unexpected(Error{"unsupported", "terminals require POSIX pseudo-terminals"});
}
Result<void> Terminals::write(std::int64_t, std::string_view) {
    return std::unexpected(Error{"unsupported", "terminals require POSIX pseudo-terminals"});
}
Result<void> Terminals::resize(std::int64_t, TerminalSize) {
    return std::unexpected(Error{"unsupported", "terminals require POSIX pseudo-terminals"});
}
void Terminals::close_all() {}

#endif

}  // namespace liteapp::workspace
