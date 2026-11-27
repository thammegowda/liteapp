#include "liteapp/app/desktop.h"

#include <utility>

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

}  // namespace

Result<void> run_desktop_application(HttpServer& server, DesktopApplicationOptions options) {
    if (options.app_id.empty() || options.title.empty()) {
        return std::unexpected(Error{"invalid_application", "desktop application ID and title must not be empty"});
    }
    const auto port = server.bind();
    if (!port) return std::unexpected(port.error());
    auto running = server.start();
    if (!running) return std::unexpected(running.error());
    if (options.on_started) options.on_started();
    const auto launch_url = server.local_launch_url(*port);
    if (options.on_ready) options.on_ready(launch_url);

    if (!options.development && options.open_window && local_display()) {
        if (platform::webview_window_supported()) {
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
            if (opened) {
                server.stop();
                return running->wait();
            }
            if (options.on_open_failed) options.on_open_failed(opened.error());
        } else if (const auto opened = platform::open_url(launch_url); !opened) {
            if (options.on_open_failed) options.on_open_failed(opened.error());
        }
    }
    return running->wait();
}

}  // namespace liteapp
