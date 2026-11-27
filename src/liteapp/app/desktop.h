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
};

[[nodiscard]] Result<void> run_desktop_application(
    HttpServer& server,
    DesktopApplicationOptions options);

}  // namespace liteapp
