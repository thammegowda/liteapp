#include "liteapp/platform/webview_window.h"

namespace liteapp::platform {

bool webview_window_supported() noexcept { return false; }

Result<void> run_webview_window(WebViewWindowOptions) {
    return std::unexpected(Error{"unsupported_platform", "embedded WebView requires native Windows"});
}

}  // namespace liteapp::platform
