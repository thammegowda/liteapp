#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include "liteapp/core/result.h"
#include "liteapp/http/server.h"

namespace liteapp {

struct DesktopApplicationOptions {
    std::string app_id;
    std::string title;
    std::filesystem::path profile_directory;
    bool development{false};
    bool open_window{true};
    int width{1280};
    int height{820};
    int minimum_width{900};
    int minimum_height{600};
    // Receives the one-time launch URL. Without a local display (an SSH session, or Linux with neither DISPLAY nor
    // WAYLAND_DISPLAY) no window or browser is opened, and the app keeps serving for a browser elsewhere, e.g.
    // through a forwarded port; the same holds when opening fails.
    std::function<void(std::string_view)> on_ready;
    std::function<void()> on_started;
    // Called when a window or browser could not be opened; the server keeps running.
    std::function<void(const Error&)> on_open_failed;
    // Called once before the server stops, when the window closes or, without a window, on Ctrl-C (SIGINT, SIGTERM
    // or SIGHUP on POSIX). Close long-lived requests (event streams, terminals) here so connections can finish.
    std::function<void()> on_stopping;
};

// Binds and starts `server`, opens the launch URL in a native window (Windows WebView2) or the default browser, and
// serves until the window closes, the process receives a stop signal, or the application stops the server. Without a
// window the calling thread blocks the stop signals before the server starts, so server threads and threads started
// later inherit that mask, and a helper thread receives them.
[[nodiscard]] Result<void> run_desktop_application(
    HttpServer& server,
    DesktopApplicationOptions options);

}  // namespace liteapp
