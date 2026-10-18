#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include <liteapp/dual_host_assets.h>

#include "liteapp/app/desktop.h"
#include "liteapp/core/random.h"
#include "liteapp/http/server.h"
#include "liteapp/platform/openers.h"
#include "liteapp/service/router.h"
#include "liteapp/vfs/filesystem.h"

auto main() -> int {
    const auto token = liteapp::secure_random_token();
    if (!token) {
        std::cerr << token.error().message << '\n';
        return 1;
    }
    auto created = liteapp::HttpServer::create({
        .host = "127.0.0.1",
        .port = 0,
        .local_access = liteapp::LocalAccess{.token = *token, .cookie_name = "liteapp_skeleton_access"},
    });
    if (!created) {
        std::cerr << created.error().message << '\n';
        return 1;
    }
    auto server = std::move(*created);
    liteapp::ServiceRouter services;
    services.register_method("app.status", [](liteapp::ServiceContext&) -> liteapp::Result<nlohmann::json> {
        return nlohmann::json{{"variant", "desktop"}, {"webgpu", false}, {"wasm", false}};
    });
    services.register_method("app.echo", [](liteapp::ServiceContext& context) -> liteapp::Result<nlohmann::json> {
        return nlohmann::json{
            {"variant", "desktop"},
            {"value", context.payload().value("value", "")},
        };
    });
    server.websocket(
        "/services",
        [&services](const liteapp::Request&, liteapp::WebSocket& socket) { services.run(socket); });
    const auto assets = liteapp::vfs::FileSystem::from_zip(liteapp::dual_host_assets::archive());
    if (!assets) {
        std::cerr << assets.error().message << '\n';
        return 1;
    }
    if (const auto served = server.serve_static(*assets); !served) {
        std::cerr << served.error().message << '\n';
        return 1;
    }
    const auto headless = liteapp::platform::environment("LITEAPP_SKELETON_NO_OPEN").has_value();
    liteapp::DesktopApplicationOptions options;
    options.app_id = "liteapp.dual-host";
    options.title = "LiteApp dual-host skeleton";
    options.profile_directory = std::filesystem::temp_directory_path() / "liteapp-dual-host";
    options.development = headless;
    options.on_ready = [](std::string_view url) { std::cout << url << std::endl; };
    const auto ran = liteapp::run_desktop_application(server, std::move(options));
    if (!ran) {
        std::cerr << ran.error().message << '\n';
        return 1;
    }
    return 0;
}
