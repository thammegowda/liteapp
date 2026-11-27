#include <optional>
#include <span>
#include <string>

#include <gtest/gtest.h>
#include <httplib.h>

#include "liteapp/http/server.h"

namespace {

class FrameworkWebSocket : public ::testing::Test {
protected:
    void SetUp() override {
        auto created = liteapp::HttpServer::create({
            .host = "127.0.0.1",
            .port = 0,
            .max_body_bytes = 1024,
            .local_access = liteapp::LocalAccess{.token = "websocket-test", .cookie_name = "liteapp_test_access"},
        });
        ASSERT_TRUE(created) << created.error().message;
        server.emplace(std::move(*created));
        server->websocket(
            "/ws",
            [](const liteapp::Request&, liteapp::WebSocket& socket) {
                const auto message = socket.read();
                if (!message || !*message) return;
                if ((**message).kind == liteapp::WebSocketMessageKind::text) {
                    static_cast<void>(socket.send_text("echo:" + (**message).data));
                } else {
                    static_cast<void>(socket.send_binary(std::as_bytes(std::span((**message).data))));
                }
            },
            {.max_text_bytes = 64, .max_binary_bytes = 4});
        const auto bound = server->bind();
        ASSERT_TRUE(bound) << bound.error().message;
        port = *bound;
        auto started = server->start();
        ASSERT_TRUE(started) << started.error().message;
        running.emplace(std::move(*started));

        httplib::Client http{"127.0.0.1", port};
        http.set_follow_location(false);
        const auto launch = http.Get("/launch/websocket-test");
        ASSERT_TRUE(launch);
        ASSERT_EQ(launch->status, 303) << "the launch URL exchanges the capability for a cookie";
        const auto cookie =
            launch->get_header_value("Set-Cookie").substr(0, std::string{"liteapp_test_access=websocket-test"}.size());
        headers = {{"Cookie", cookie}, {"Origin", "http://127.0.0.1:" + std::to_string(port)}};
    }

    [[nodiscard]] std::string url() const { return "ws://127.0.0.1:" + std::to_string(port) + "/ws"; }

    std::optional<liteapp::HttpServer> server;
    std::optional<liteapp::RunningHttpServer> running;
    int port{0};
    httplib::Headers headers;
};

}  // namespace

TEST_F(FrameworkWebSocket, RejectsUnauthenticatedClients) {
    httplib::ws::WebSocketClient rejected{url()};
    EXPECT_FALSE(rejected.connect());
}

TEST_F(FrameworkWebSocket, EchoesTextAndBinary) {
    httplib::ws::WebSocketClient text{url(), headers};
    text.set_read_timeout(5);
    ASSERT_TRUE(text.connect());
    ASSERT_TRUE(text.send("hello"));
    std::string response;
    ASSERT_EQ(text.read(response), httplib::ws::ReadResult::Text);
    EXPECT_EQ(response, "echo:hello");
    text.close();

    httplib::ws::WebSocketClient binary{url(), headers};
    binary.set_read_timeout(5);
    const std::string payload{"\0pc\1", 4};
    ASSERT_TRUE(binary.connect());
    ASSERT_TRUE(binary.send(payload.data(), payload.size()));
    response.clear();
    ASSERT_EQ(binary.read(response), httplib::ws::ReadResult::Binary);
    EXPECT_EQ(response, payload) << "binary payloads are preserved byte for byte";
    binary.close();
}

TEST_F(FrameworkWebSocket, ClosesOnOversizedMessages) {
    httplib::ws::WebSocketClient oversized{url(), headers};
    oversized.set_read_timeout(5);
    ASSERT_TRUE(oversized.connect());
    ASSERT_TRUE(oversized.send(std::string(65, 'x')));
    std::string response;
    EXPECT_EQ(oversized.read(response), httplib::ws::ReadResult::Fail);
}
