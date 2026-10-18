#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include "liteapp/core/result.h"
#include "liteapp/vfs/filesystem.h"

namespace liteapp {

class HttpServer;

class RunningHttpServer {
public:
    ~RunningHttpServer();
    RunningHttpServer(RunningHttpServer&&) noexcept;
    RunningHttpServer& operator=(RunningHttpServer&&) noexcept;
    RunningHttpServer(const RunningHttpServer&) = delete;
    RunningHttpServer& operator=(const RunningHttpServer&) = delete;

    [[nodiscard]] Result<void> wait();

private:
    friend class HttpServer;
    struct Impl;

    explicit RunningHttpServer(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

struct Request {
    std::string method;
    std::string path;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::unordered_map<std::string, std::string> query;

    [[nodiscard]] std::string header(std::string_view name) const;
    [[nodiscard]] std::string query_parameter(std::string_view name) const;
};

struct Response {
    int status{200};
    std::string content_type{"text/plain; charset=utf-8"};
    std::string body;
    std::unordered_map<std::string, std::string> headers;

    [[nodiscard]] static Response text(std::string body, int status = 200);
    [[nodiscard]] static Response json(std::string body, int status = 200);
};

using Handler = std::function<Response(const Request&)>;

enum class WebSocketMessageKind {
    text,
    binary,
};

struct WebSocketMessage {
    WebSocketMessageKind kind;
    std::string data;
};

struct WebSocketOptions {
    std::size_t max_text_bytes{1024U * 1024U};
    std::size_t max_binary_bytes{1024U * 1024U};
};

class WebSocket {
public:
    ~WebSocket();
    WebSocket(const WebSocket&) = delete;
    WebSocket& operator=(const WebSocket&) = delete;

    [[nodiscard]] Result<std::optional<WebSocketMessage>> read();
    [[nodiscard]] bool send_text(std::string_view data);
    [[nodiscard]] bool send_binary(std::span<const std::byte> data);
    void close(std::string_view reason = {});
    [[nodiscard]] bool is_open() const;

private:
    friend class HttpServer;
    struct Impl;

    explicit WebSocket(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

using WebSocketHandler = std::function<void(const Request&, WebSocket&)>;

struct LocalAccess {
    std::string token;
    std::string cookie_name{"copilot_chats_access"};
    std::string query_parameter{"token"};
};

struct ServerOptions {
    std::string host{"127.0.0.1"};
    int port{0};
    std::size_t max_body_bytes{8U * 1024U * 1024U};
    std::optional<LocalAccess> local_access;
    std::string content_security_policy{
        "default-src 'self'; base-uri 'none'; connect-src 'self'; form-action 'self'; frame-ancestors 'none'; "
        "img-src 'self' data:; script-src 'self'; style-src 'self'"};
};

struct StaticFile {
    std::string route;
    std::filesystem::path path;
    std::string content_type{};
};

struct StaticManifestOptions {
    std::string route_prefix{"/"};
    std::string index_file{"index.html"};
    std::size_t max_files{1024};
    std::size_t max_manifest_bytes{1024U * 1024U};
};

class HttpServer {
public:
    [[nodiscard]] static Result<HttpServer> create(ServerOptions options = {});

    ~HttpServer();
    HttpServer(HttpServer&&) noexcept;
    HttpServer& operator=(HttpServer&&) noexcept;
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void get(std::string path, Handler handler);
    void post(std::string path, Handler handler);
    void websocket(std::string path, WebSocketHandler handler, WebSocketOptions options = {});
    [[nodiscard]] Result<void> serve_static(const std::filesystem::path& root);
    [[nodiscard]] Result<void> serve_static(vfs::FileSystem filesystem);
    [[nodiscard]] Result<void> serve_files(std::span<const StaticFile> files);
    [[nodiscard]] Result<void> serve_manifest(
        const std::filesystem::path& root,
        const std::filesystem::path& manifest,
        StaticManifestOptions options = {});
    void serve_file(std::string route, vfs::FileSystem filesystem, std::string path);
    [[nodiscard]] Result<int> bind();
    [[nodiscard]] Result<RunningHttpServer> start();
    [[nodiscard]] Result<void> listen_after_bind();
    [[nodiscard]] Result<void> listen();
    void stop();
    void wait_until_ready() const;
    [[nodiscard]] std::string local_launch_url(int port) const;

private:
    struct Impl;

    HttpServer(ServerOptions options, std::unique_ptr<Impl> impl);

    ServerOptions options_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace liteapp