#include "liteapp/app/desktop.h"

#include <atomic>
#include <optional>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <signal.h>
#endif

#include "liteapp/platform/openers.h"
#include "liteapp/platform/webview_window.h"

namespace liteapp {
namespace {

bool local_display() {
    if (platform::environment("SSH_CONNECTION") || platform::environment("SSH_TTY")) return false;
#if defined(__linux__)
    return platform::environment("DISPLAY") || platform::environment("WAYLAND_DISPLAY");
#else
    return true;
#endif
}

// Ctrl-C and termination requests. On POSIX the constructor blocks the signals in the calling thread, so threads
// started afterwards inherit the mask; watch() then receives them on a helper thread, which calls `on_signal` once.
// The destructor stops watching and restores the mask.
class StopSignals {
public:
    StopSignals(const StopSignals&) = delete;
    StopSignals& operator=(const StopSignals&) = delete;

#if defined(_WIN32)
    StopSignals() {
        state_ = WAITING;
        SetConsoleCtrlHandler(&StopSignals::request, TRUE);
    }
    ~StopSignals() {
        if (watcher_.joinable()) {
            int waiting = WAITING;
            state_.compare_exchange_strong(waiting, CANCELLED);
            state_.notify_all();
            watcher_.join();
        }
        SetConsoleCtrlHandler(&StopSignals::request, FALSE);
    }
    void watch(std::function<void()> on_signal) {
        watcher_ = std::thread([on_signal = std::move(on_signal)] {
            state_.wait(WAITING);
            if (state_ == REQUESTED) on_signal();
        });
    }

private:
    enum : int { WAITING, REQUESTED, CANCELLED };
    static BOOL WINAPI request(DWORD) {
        int waiting = WAITING;
        state_.compare_exchange_strong(waiting, REQUESTED);
        state_.notify_all();
        return TRUE;
    }
    static inline std::atomic<int> state_{WAITING};
#else
    StopSignals() {
        sigemptyset(&signals_);
        sigaddset(&signals_, SIGINT);
        sigaddset(&signals_, SIGTERM);
        sigaddset(&signals_, SIGHUP);
        ::pthread_sigmask(SIG_BLOCK, &signals_, &previous_);
    }
    ~StopSignals() {
        if (watcher_.joinable()) {
            // A thread-directed SIGTERM stays pending until the watcher's sigwait takes it.
            cancelled_ = true;
            ::pthread_kill(watcher_.native_handle(), SIGTERM);
            watcher_.join();
        }
        ::pthread_sigmask(SIG_SETMASK, &previous_, nullptr);
    }
    void watch(std::function<void()> on_signal) {
        watcher_ = std::thread([this, on_signal = std::move(on_signal)] {
            int received = 0;
            ::sigwait(&signals_, &received);
            if (!cancelled_) on_signal();
        });
    }

private:
    sigset_t signals_{};
    sigset_t previous_{};
    std::atomic<bool> cancelled_{false};
#endif
    std::thread watcher_;
};

}  // namespace

Result<void> run_desktop_application(HttpServer& server, DesktopApplicationOptions options) {
    if (options.app_id.empty() || options.title.empty()) {
        return std::unexpected(Error{"invalid_application", "desktop application ID and title must not be empty"});
    }
    const bool show = !options.development && options.open_window && local_display();
    const bool window = show && platform::webview_window_supported();
    std::optional<StopSignals> signals;
    if (!window) signals.emplace();
    const auto port = server.bind();
    if (!port) return std::unexpected(port.error());
    auto running = server.start();
    if (!running) return std::unexpected(running.error());
    const auto stop = [&server, &options] {
        if (options.on_stopping) options.on_stopping();
        server.stop();
    };
    if (signals) signals->watch(stop);
    if (options.on_started) options.on_started();
    const auto launch_url = server.local_launch_url(*port);
    if (options.on_ready) options.on_ready(launch_url);

    if (window) {
        const auto opened = platform::run_webview_window({
            .app_id = std::move(options.app_id),
            .title = std::move(options.title),
            .launch_url = launch_url,
            .allowed_origin = "http://127.0.0.1:" + std::to_string(*port),
            .profile_directory = std::move(options.profile_directory),
            .width = options.width,
            .height = options.height,
            .minimum_width = options.minimum_width,
            .minimum_height = options.minimum_height,
        });
        if (opened) stop();
        else if (options.on_open_failed) options.on_open_failed(opened.error());
    } else if (show) {
        if (const auto opened = platform::open_url(launch_url); !opened && options.on_open_failed) {
            options.on_open_failed(opened.error());
        }
    }
    // Returns when the server stops: after a stop signal or window close, or when the application stops it.
    return running->wait();
}

}  // namespace liteapp
