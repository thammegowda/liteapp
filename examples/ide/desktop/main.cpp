// A minimal local IDE for one directory: file tree, tabbed editor with Git diffs, and a terminal. The browser UI is
// composed from LiteApp's web/ide modules; this host only wires liteapp::workspace services to an HTTP server.
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <liteapp/ide_assets.h>

#include "liteapp/app/desktop.h"
#include "liteapp/core/random.h"
#include "liteapp/http/server.h"
#include "liteapp/platform/openers.h"
#include "liteapp/service/event_hub.h"
#include "liteapp/service/router.h"
#include "liteapp/workspace/services.h"

auto main(int argc, char** argv) -> int {
    std::error_code error;
    const auto root = std::filesystem::canonical(argc > 1 ? argv[1] : ".", error);
    auto workspace = error ? liteapp::Result<liteapp::workspace::Workspace>(std::unexpected(
                                 liteapp::Error{"invalid", "no such directory"}))
                           : liteapp::workspace::Workspace::open(root);
    if (!workspace) {
        std::cerr << "usage: liteapp-ide [DIRECTORY]: " << workspace.error().message << '\n';
        return 1;
    }
    const auto token = liteapp::secure_random_token();
    if (!token) {
        std::cerr << token.error().message << '\n';
        return 1;
    }
    auto created = liteapp::HttpServer::create({
        .host = "127.0.0.1",
        .port = 0,
        .max_body_bytes = 64U * 1024U,
        .local_access = liteapp::LocalAccess{.token = *token, .cookie_name = "liteapp_ide_access"},
        // CodeMirror and xterm.js insert their own style sheets.
        .content_security_policy =
            "default-src 'self'; base-uri 'none'; connect-src 'self'; form-action 'self'; frame-ancestors 'none'; "
            "img-src 'self' data:; script-src 'self'; style-src 'self' 'unsafe-inline'",
    });
    if (!created) {
        std::cerr << created.error().message << '\n';
        return 1;
    }
    auto server = std::move(*created);

    liteapp::EventHub events;
    liteapp::workspace::WorkspaceServices files(
        *workspace, {.files_written = [&events](const std::vector<std::string>& paths) {
            events.publish("files_changed", {{"paths", paths}});
        }});
    liteapp::ServiceRouter router;
    files.register_methods(router);
    router.register_async_method(
        "events.subscribe",
        [&events](liteapp::ServiceContext&, liteapp::ServiceResponder responder) {
            events.subscribe(std::move(responder));
        });
    router.register_method("app.ping", [](liteapp::ServiceContext&) -> liteapp::Result<nlohmann::json> {
        return nlohmann::json::object();
    });
    router.register_method("app.info", [&root](liteapp::ServiceContext&) -> liteapp::Result<nlohmann::json> {
        return nlohmann::json{{"name", root.filename().string()}, {"workspace", root.string()}};
    });
    server.websocket(
        "/ws",
        [&router](const liteapp::Request&, liteapp::WebSocket& socket) { router.run(socket); },
        {.max_text_bytes = 16U * 1024U * 1024U, .max_binary_bytes = 64U * 1024U});

    // LITEAPP_IDE_SOURCE=<LiteApp checkout> serves the UI from disk, so edits need only a page reload.
    const auto source = liteapp::platform::environment("LITEAPP_IDE_SOURCE");
    const auto manifest = std::filesystem::path(source.value_or("")) / "examples/ide/web/assets.txt";
    const auto served = source ? server.serve_manifest(*source, manifest)
                               : server.serve_archive(liteapp::ide_assets::archive());
    if (!served) {
        std::cerr << served.error().message << '\n';
        return 1;
    }
    liteapp::DesktopApplicationOptions options;
    options.app_id = "liteapp.ide";
    options.title = "LiteApp IDE — " + root.filename().string();
    options.profile_directory = std::filesystem::temp_directory_path() / "liteapp-ide";
    options.development = liteapp::platform::environment("LITEAPP_IDE_NO_OPEN").has_value();
    options.on_ready = [](std::string_view url) {
        std::cout << "Open " << url << "\n(the link works once; over SSH, forward the port first)" << std::endl;
    };
    options.on_open_failed = [](const liteapp::Error& failure) {
        std::cerr << "Could not open a browser (" << failure.message << "); open the link above yourself." << std::endl;
    };
    options.on_stopping = [&files, &events] {
        files.close();
        events.close();
    };
    const auto ran = liteapp::run_desktop_application(server, std::move(options));
    if (!ran) {
        std::cerr << ran.error().message << '\n';
        return 1;
    }
    return 0;
}
