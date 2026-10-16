#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <httplib.h>

#include <liteapp/test_assets.h>

#include "liteapp/http/server.h"
#include "liteapp/vfs/filesystem.h"

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("liteapp-http-" + std::to_string(id));
        std::filesystem::create_directories(path);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    std::filesystem::path path;
};

}  // namespace

TEST(FrameworkHttp, ProtectsLocalRoutesWithCapabilityCookie) {
    TemporaryDirectory temporary;
    std::ofstream{temporary.path / "index.html"} << "protected UI";

    auto created = liteapp::HttpServer::create(liteapp::ServerOptions{
        .host = "127.0.0.1",
        .port = 0,
        .max_body_bytes = 1024,
        .local_access = liteapp::LocalAccess{.token = "test-capability"},
    });
    ASSERT_TRUE(created);
    auto server = std::move(*created);
    server.get("/private", [](const liteapp::Request&) {
        return liteapp::Response::json(R"({"ok":true})");
    });
    server.get("/query", [](const liteapp::Request& request) {
        return liteapp::Response::text(request.query_parameter("session"));
    });
    server.post("/action", [](const liteapp::Request& request) {
        return liteapp::Response::text(request.method, 204);
    });
    ASSERT_TRUE(server.serve_static(temporary.path));

    const auto bound = server.bind();
    ASSERT_TRUE(bound);
    std::jthread server_thread{[&server] { static_cast<void>(server.listen_after_bind()); }};
    server.wait_until_ready();

    httplib::Client client{"127.0.0.1", *bound};
    client.set_follow_location(false);
    const auto rejected = client.Get("/private");
    ASSERT_TRUE(rejected);
    EXPECT_EQ(rejected->status, 403);

    EXPECT_EQ(server.local_launch_url(*bound),
              "http://127.0.0.1:" + std::to_string(*bound) + "/?token=test-capability");

    const auto invalid_launch = client.Get("/?token=wrong-capability");
    ASSERT_TRUE(invalid_launch);
    EXPECT_EQ(invalid_launch->status, 403);

    const auto launch = client.Get("/?token=test-capability");
    ASSERT_TRUE(launch);
    EXPECT_EQ(launch->status, 303);
    EXPECT_EQ(launch->get_header_value("Location"), "/");
    EXPECT_EQ(launch->get_header_value("Set-Cookie"),
              "copilot_chats_access=test-capability; Path=/; HttpOnly; SameSite=Strict");

    const auto replayed_launch = client.Get("/?token=test-capability");
    ASSERT_TRUE(replayed_launch);
    EXPECT_EQ(replayed_launch->status, 403);

    const httplib::Headers authorized{{"Cookie", "copilot_chats_access=test-capability"}};
    const auto accepted = client.Get("/private", authorized);
    ASSERT_TRUE(accepted);
    EXPECT_EQ(accepted->status, 200);
    EXPECT_EQ(accepted->get_header_value("Cache-Control"), "no-store");
    EXPECT_EQ(accepted->get_header_value("X-Content-Type-Options"), "nosniff");
    EXPECT_EQ(accepted->get_header_value("X-Frame-Options"), "DENY");

    const auto still_rejected = client.Get("/private");
    ASSERT_TRUE(still_rejected);
    EXPECT_EQ(still_rejected->status, 403);

    const auto protected_asset = client.Get("/");
    ASSERT_TRUE(protected_asset);
    EXPECT_EQ(protected_asset->status, 403);

    const auto accepted_asset = client.Get("/", authorized);
    ASSERT_TRUE(accepted_asset);
    EXPECT_EQ(accepted_asset->status, 200);
    EXPECT_EQ(accepted_asset->body, "protected UI");

    const auto query = client.Get("/query?session=thread%201", authorized);
    ASSERT_TRUE(query);
    EXPECT_EQ(query->status, 200);
    EXPECT_EQ(query->body, "thread 1");

    const auto action = client.Post("/action", authorized, "", "application/json");
    ASSERT_TRUE(action);
    EXPECT_EQ(action->status, 204);

    server.stop();
}

TEST(FrameworkHttp, RejectsUnsafeLocalConfigurationAndSpoofedOrigin) {
    const auto exposed = liteapp::HttpServer::create(liteapp::ServerOptions{
        .host = "0.0.0.0",
        .port = 0,
        .max_body_bytes = 1024,
        .local_access = liteapp::LocalAccess{.token = "test-capability"},
    });
    ASSERT_FALSE(exposed);
    EXPECT_EQ(exposed.error().code, "unsafe_bind");

    const auto unsafe_token = liteapp::HttpServer::create(liteapp::ServerOptions{
        .host = "127.0.0.1",
        .port = 0,
        .max_body_bytes = 1024,
        .local_access = liteapp::LocalAccess{.token = "not cookie safe"},
    });
    ASSERT_FALSE(unsafe_token);
    EXPECT_EQ(unsafe_token.error().code, "invalid_capability");

    auto created = liteapp::HttpServer::create(liteapp::ServerOptions{
        .host = "127.0.0.1",
        .port = 0,
        .max_body_bytes = 1024,
        .local_access = liteapp::LocalAccess{.token = "test-capability"},
    });
    ASSERT_TRUE(created);
    auto server = std::move(*created);
    server.get("/private", [](const liteapp::Request&) { return liteapp::Response::text("ok"); });
    const auto bound = server.bind();
    ASSERT_TRUE(bound);
    std::jthread server_thread{[&server] { static_cast<void>(server.listen_after_bind()); }};
    server.wait_until_ready();

    httplib::Client client{"127.0.0.1", *bound};
    const httplib::Headers headers{{"Cookie", "copilot_chats_access=test-capability"},
                                   {"Origin", "https://evil.example"}};
    const auto response = client.Get("/private", headers);
    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 403);

    server.stop();
}

TEST(FrameworkHttp, ServesAuthenticatedVirtualFilesAndFallback) {
    auto created = liteapp::HttpServer::create(liteapp::ServerOptions{
        .host = "127.0.0.1",
        .port = 0,
        .max_body_bytes = 1024,
        .local_access = liteapp::LocalAccess{.token = "test-capability"},
    });
    ASSERT_TRUE(created);
    auto server = std::move(*created);
    const auto filesystem = liteapp::vfs::FileSystem::from_zip(liteapp::test_assets::archive());
    ASSERT_TRUE(filesystem);
    server.serve_file(R"(/workspace/thread/[1-9][0-9]*)", *filesystem, "index.html");
    ASSERT_TRUE(server.serve_static(*filesystem));

    const auto bound = server.bind();
    ASSERT_TRUE(bound);
    std::jthread server_thread{[&server] { static_cast<void>(server.listen_after_bind()); }};
    server.wait_until_ready();

    httplib::Client client{"127.0.0.1", *bound};
    const httplib::Headers authorized{{"Cookie", "copilot_chats_access=test-capability"}};
    const auto root = client.Get("/", authorized);
    ASSERT_TRUE(root);
    EXPECT_EQ(root->status, 200);
    EXPECT_EQ(root->get_header_value("Content-Type"), "text/html; charset=utf-8");
    EXPECT_NE(root->body.find("embedded fixture"), std::string::npos);

    const auto fallback = client.Get("/workspace/thread/2", authorized);
    ASSERT_TRUE(fallback);
    EXPECT_EQ(fallback->status, 200);
    EXPECT_NE(fallback->body.find("embedded fixture"), std::string::npos);

    const auto missing = client.Get("/missing.css", authorized);
    ASSERT_TRUE(missing);
    EXPECT_EQ(missing->status, 404);
    const auto rejected = client.Get("/other.html");
    ASSERT_TRUE(rejected);
    EXPECT_EQ(rejected->status, 403);

    server.stop();
}

TEST(FrameworkHttp, RunningHandleIsSafeAfterServerDestruction) {
    std::optional<liteapp::RunningHttpServer> running;
    {
        auto created = liteapp::HttpServer::create();
        ASSERT_TRUE(created);
        auto server = std::move(*created);
        server.get("/health", [](const liteapp::Request&) {
            return liteapp::Response::text("ok");
        });
        const auto bound = server.bind();
        ASSERT_TRUE(bound);
        auto started = server.start();
        ASSERT_TRUE(started);
        running.emplace(std::move(*started));

        httplib::Client client{"127.0.0.1", *bound};
        const auto response = client.Get("/health");
        ASSERT_TRUE(response);
        EXPECT_EQ(response->status, 200);
        EXPECT_EQ(response->body, "ok");
    }

    ASSERT_TRUE(running);
    EXPECT_TRUE(running->wait());
}