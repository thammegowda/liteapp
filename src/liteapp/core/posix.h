#pragma once

// Small POSIX helpers shared by LiteApp's file, watcher and process code. Include only on POSIX platforms.

#include <signal.h>
#include <unistd.h>

#include <utility>

namespace liteapp::posix {

// Owns a file descriptor and closes it once.
class FileDescriptor {
public:
    explicit FileDescriptor(int value = -1) noexcept : value_{value} {}
    ~FileDescriptor() { reset(); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept : value_{std::exchange(other.value_, -1)} {}
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            reset();
            value_ = std::exchange(other.value_, -1);
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return value_; }
    [[nodiscard]] explicit operator bool() const noexcept { return value_ >= 0; }
    void reset() noexcept {
        if (value_ >= 0) ::close(std::exchange(value_, -1));
    }

private:
    int value_;
};

// For a forked child before exec: hosts often block or ignore signals (for example to sigwait for Ctrl-C), and a
// child inherits that, so shells and commands would ignore Ctrl-C and broken pipes. Async-signal-safe.
inline void reset_child_signals() noexcept {
    sigset_t none;
    sigemptyset(&none);
    ::sigprocmask(SIG_SETMASK, &none, nullptr);
    ::signal(SIGPIPE, SIG_DFL);
}

}  // namespace liteapp::posix
