#include <atomic>
#include <array>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "liteapp/http/server.h"
#include "liteapp/service/router.h"

namespace {
using Json = nlohmann::json;

Json read_json(httplib::ws::WebSocketClient& socket) {
    std::string message;
    if (socket.read(message) != httplib::ws::ReadResult::Text) return {};
    return Json::parse(message);
}
}  // namespace

TEST(FrameworkService, RoutesProgressCompletionCancellationAndBinaryFrames) {
    liteapp::ServiceRouter router;
    router.register_method("math.double", [](liteapp::ServiceContext& context) -> liteapp::Result<Json> {
        static_cast<void>(context.emit("progress", {{"value", context.payload().value("value", 0)}}));
        return Json{{"value", context.payload().value("value", 0) * 2}};
    });
    router.register_method("timer.wait", [](liteapp::ServiceContext& context) -> liteapp::Result<Json> {
        static_cast<void>(context.emit("started"));
        while (!context.cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds{1});
        return Json::object();
    });
    std::atomic_size_t binary_bytes{0};
    router.set_binary_handler([&](std::span<const std::byte> bytes) { binary_bytes += bytes.size(); });

    auto created = liteapp::HttpServer::create();
    ASSERT_TRUE(created);
    auto server = std::move(*created);
    server.websocket("/services", [&](const liteapp::Request&, liteapp::WebSocket& socket) { router.run(socket); });
    const auto port = server.bind();
    ASSERT_TRUE(port);
    std::jthread server_thread{[&server] { static_cast<void>(server.listen_after_bind()); }};
    server.wait_until_ready();

    httplib::ws::WebSocketClient socket{"ws://127.0.0.1:" + std::to_string(*port) + "/services"};
    ASSERT_TRUE(socket.connect());
    ASSERT_TRUE(socket.send(Json{
        {"version", 1}, {"kind", "request"}, {"id", "double"}, {"method", "math.double"},
        {"payload", {{"value", 4}}},
    }.dump()));
    const auto progress = read_json(socket);
    EXPECT_EQ(progress["type"], "progress");
    EXPECT_EQ(progress["sequence"], 0);
    const auto completed = read_json(socket);
    EXPECT_EQ(completed["type"], "completed");
    EXPECT_EQ(completed["sequence"], 1);
    EXPECT_EQ(completed["payload"]["value"], 8);

    ASSERT_TRUE(socket.send(Json{
        {"version", 1}, {"kind", "request"}, {"id", "slow"}, {"method", "timer.wait"},
        {"payload", Json::object()},
    }.dump()));
    EXPECT_EQ(read_json(socket)["type"], "started");
    ASSERT_TRUE(socket.send(Json{{"version", 1}, {"kind", "cancel"}, {"id", "slow"}}.dump()));
    EXPECT_EQ(read_json(socket)["type"], "cancelled");

    constexpr std::array payload{std::byte{1}, std::byte{2}, std::byte{3}};
    ASSERT_TRUE(socket.send(reinterpret_cast<const char*>(payload.data()), payload.size()));
    for (int attempt = 0; attempt < 100 && binary_bytes.load() != payload.size(); ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    EXPECT_EQ(binary_bytes.load(), payload.size());

    socket.close();
    server.stop();
}

TEST(FrameworkService, InProcessSessionUsesTheSameProtocol) {
    liteapp::ServiceRouter router;
    router.register_method("math.double", [](liteapp::ServiceContext& context) -> liteapp::Result<Json> {
        static_cast<void>(context.emit("progress", {{"value", context.payload().value("value", 0)}}));
        return Json{{"value", context.payload().value("value", 0) * 2}};
    });
    std::atomic_size_t binary_bytes{0};
    router.set_binary_handler([&](std::span<const std::byte> bytes) { binary_bytes += bytes.size(); });
    std::mutex messages_mutex;
    std::vector<Json> messages;
    std::string closed;
    auto session = router.open({
        .send_text = [&](std::string_view message) {
            std::scoped_lock lock{messages_mutex};
            messages.push_back(Json::parse(message));
            return true;
        },
        .send_binary = [](std::span<const std::byte>) { return true; },
        .close = [&](std::string_view reason) { closed = reason; },
    });
    ASSERT_TRUE(session->receive_text(Json{
        {"version", 1}, {"kind", "request"}, {"id", "double"}, {"method", "math.double"},
        {"payload", {{"value", 5}}},
    }.dump()));
    for (int attempt = 0; attempt < 100; ++attempt) {
        {
            std::scoped_lock lock{messages_mutex};
            if (messages.size() == 2) break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    {
        std::scoped_lock lock{messages_mutex};
        ASSERT_EQ(messages.size(), 2);
        EXPECT_EQ(messages[0]["type"], "progress");
        EXPECT_EQ(messages[1]["type"], "completed");
        EXPECT_EQ(messages[1]["payload"]["value"], 10);
    }
    constexpr std::array payload{std::byte{1}, std::byte{2}};
    EXPECT_TRUE(session->receive_binary(payload));
    EXPECT_EQ(binary_bytes.load(), payload.size());
    EXPECT_FALSE(session->receive_text("{invalid"));
    EXPECT_EQ(closed, "invalid service envelope");
}
