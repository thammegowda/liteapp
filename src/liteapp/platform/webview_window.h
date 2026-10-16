#pragma once

#include <filesystem>
#include <string>

#include "liteapp/core/result.h"
#include "liteapp/platform/single_instance.h"

namespace liteapp::platform {

struct WebViewWindowOptions {
    std::string app_id;
    std::string title;
    std::string launch_url;
    std::string allowed_origin;
    std::filesystem::path profile_directory;
    SingleInstance* instance{nullptr};
    int width{1280};
    int height{820};
    int minimum_width{900};
    int minimum_height{600};
};

[[nodiscard]] bool webview_window_supported() noexcept;
[[nodiscard]] Result<void> run_webview_window(WebViewWindowOptions options);

}  // namespace liteapp::platform
