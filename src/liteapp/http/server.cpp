#include "liteapp/http/server.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

#include <httplib.h>

namespace liteapp {
namespace {

bool header_name_equal(std::string_view left, std::string_view right) noexcept {
    return left.size() == right.size() && std::ranges::equal(left, right, [](char lhs, char rhs) {
        return std::tolower(static_cast<unsigned char>(lhs)) == std::tolower(static_cast<unsigned char>(rhs));
    });
}

bool constant_time_equal(std::string_view left, std::string_view right) noexcept {
    const auto length = std::max(left.size(), right.size());
    std::uint8_t difference = static_cast<std::uint8_t>(left.size() != right.size());
    for (std::size_t index = 0; index < length; ++index) {
        const auto lhs = index < left.size() ? static_cast<std::uint8_t>(left[index]) : 0U;
        const auto rhs = index < right.size() ? static_cast<std::uint8_t>(right[index]) : 0U;
        difference = static_cast<std::uint8_t>(difference | (lhs ^ rhs));
    }
    return difference == 0U;
}

bool token_component_valid(std::string_view value) noexcept {
    return !value.empty() && std::ranges::all_of(value, [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == '-' || character == '_';
    });
}

std::string_view trim(std::string_view value) noexcept {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1U);
}

std::optional<std::string_view> cookie_value(std::string_view cookies, std::string_view name) noexcept {
    std::size_t start = 0;
    while (start < cookies.size()) {
        const auto end = cookies.find(';', start);
        const auto item = trim(cookies.substr(start, end - start));
        const auto equals = item.find('=');
        if (equals != std::string_view::npos && item.substr(0, equals) == name) {
            return item.substr(equals + 1U);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1U;
    }
    return std::nullopt;
}

Request make_request(const httplib::Request& source) {
    std::unordered_map<std::string, std::string> headers;
    for (const auto& [name, value] : source.headers) {
        headers.try_emplace(name, value);
    }
    std::unordered_map<std::string, std::string> query;
    for (const auto& [name, value] : source.params) {
        query.try_emplace(name, value);
    }
    return Request{
        .method = source.method,
        .path = source.path,
        .body = source.body,
        .headers = std::move(headers),
        .query = std::move(query),
    };
}

void apply_response(httplib::Response& target, Response source) {
    target.status = source.status;
    for (auto& [name, value] : source.headers) {
        target.set_header(name, value);
    }
    target.set_content(std::move(source.body), source.content_type);
}

Response virtual_file_response(const vfs::FileSystem& filesystem, std::string_view path) {
    const auto file = filesystem.read(path);
    if (!file) return Response::text(file.error().message, 500);
    if (!*file) return Response::text("not found", 404);
    const auto& asset = **file;
    auto content_type = asset.info.media_type;
    if (asset.info.text_encoding) content_type += "; charset=" + *asset.info.text_encoding;
    std::string body(asset.info.size, '\0');
    std::ranges::transform(asset.bytes(), body.begin(), [](std::byte value) {
        return std::bit_cast<char>(value);
    });
    return Response{
        .status = 200,
        .content_type = std::move(content_type),
        .body = std::move(body),
        .headers = {},
    };
}

httplib::Server::HandlerResponse enforce_local_access(
    const ServerOptions& options,
    std::atomic_bool& launch_claimed,
    const httplib::Request& request,
    httplib::Response& response) {
    if (!options.local_access) {
        return httplib::Server::HandlerResponse::Unhandled;
    }

    const auto authority = options.host + ':' + std::to_string(request.local_port);
    if (request.get_header_value("Host") != authority) {
        apply_response(response, Response::json(R"({"error":"invalid local Host header"})", 403));
        return httplib::Server::HandlerResponse::Handled;
    }
    const auto origin = request.get_header_value("Origin");
    if (!origin.empty() && origin != "http://" + authority) {
        apply_response(response, Response::json(R"({"error":"invalid local Origin header"})", 403));
        return httplib::Server::HandlerResponse::Handled;
    }

    if (request.path == "/" && request.has_param(options.local_access->query_parameter)) {
        const auto supplied = request.get_param_value(options.local_access->query_parameter);
        if (!constant_time_equal(supplied, options.local_access->token)) {
            apply_response(response, Response::json(R"({"error":"invalid local launch capability"})", 403));
            return httplib::Server::HandlerResponse::Handled;
        }
        bool expected = false;
        if (!launch_claimed.compare_exchange_strong(expected, true)) {
            apply_response(response, Response::json(R"({"error":"local launch capability already used"})", 403));
            return httplib::Server::HandlerResponse::Handled;
        }
        response.status = 303;
        response.set_header("Location", "/");
        response.set_header(
            "Set-Cookie",
            options.local_access->cookie_name + '=' + options.local_access->token +
                "; Path=/; HttpOnly; SameSite=Strict");
        return httplib::Server::HandlerResponse::Handled;
    }

    const auto cookies = request.get_header_value("Cookie");
    const auto capability = cookie_value(cookies, options.local_access->cookie_name);
    if (!capability || !constant_time_equal(*capability, options.local_access->token)) {
        apply_response(response, Response::json(R"({"error":"local access capability required"})", 403));
        return httplib::Server::HandlerResponse::Handled;
    }
    return httplib::Server::HandlerResponse::Unhandled;
}

}  // namespace

struct HttpServer::Impl {
    std::shared_ptr<httplib::Server> server{std::make_shared<httplib::Server>()};
};

struct WebSocket::Impl {
    httplib::ws::WebSocket* socket;
    WebSocketOptions options;
};

struct RunningHttpServer::Impl {
    explicit Impl(std::shared_ptr<httplib::Server> implementation)
        : server{std::move(implementation)}, thread{[this] {
        Result<void> listen_result;
        if (!server->listen_after_bind()) {
            listen_result = std::unexpected(Error{"listen_failed", "HTTP server stopped with an error"});
        }
        std::scoped_lock lock{mutex};
        result = std::move(listen_result);
    }} {}

    ~Impl() {
        server->stop();
        if (thread.joinable()) thread.join();
    }

    std::shared_ptr<httplib::Server> server;
    std::mutex mutex;
    std::optional<Result<void>> result;
    std::jthread thread;
};

RunningHttpServer::RunningHttpServer(std::unique_ptr<Impl> impl) : impl_{std::move(impl)} {}
WebSocket::WebSocket(std::unique_ptr<Impl> impl) : impl_{std::move(impl)} {}
WebSocket::~WebSocket() = default;

Result<std::optional<WebSocketMessage>> WebSocket::read() {
    if (!impl_ || !impl_->socket->is_open()) return std::optional<WebSocketMessage>{};
    std::string data;
    const auto result = impl_->socket->read(data);
    if (result == httplib::ws::ReadResult::Fail) return std::optional<WebSocketMessage>{};
    const auto kind =
        result == httplib::ws::ReadResult::Text ? WebSocketMessageKind::text : WebSocketMessageKind::binary;
    const auto limit = kind == WebSocketMessageKind::text ? impl_->options.max_text_bytes : impl_->options.max_binary_bytes;
    if (data.size() > limit) {
        impl_->socket->close(httplib::ws::CloseStatus::MessageTooBig, "message exceeds configured limit");
        return std::unexpected(Error{"message_too_big", "WebSocket message exceeds configured limit"});
    }
    return std::optional<WebSocketMessage>{WebSocketMessage{kind, std::move(data)}};
}

bool WebSocket::send_text(std::string_view data) {
    return impl_ && impl_->socket->send(std::string(data));
}

bool WebSocket::send_binary(std::span<const std::byte> data) {
    return impl_ && impl_->socket->send(reinterpret_cast<const char*>(data.data()), data.size());
}

void WebSocket::close(std::string_view reason) {
    if (impl_) impl_->socket->close(httplib::ws::CloseStatus::Normal, std::string(reason));
}

bool WebSocket::is_open() const {
    return impl_ && impl_->socket->is_open();
}


std::string Request::header(std::string_view name) const {
    for (const auto& [header_name, value] : headers) {
        if (header_name_equal(header_name, name)) {
            return value;
        }
    }
    return {};
}

std::string Request::query_parameter(std::string_view name) const {
    const auto found = query.find(std::string{name});
    return found == query.end() ? std::string{} : found->second;
}

Response Response::text(std::string body, int status) {
    return Response{
        .status = status,
        .content_type = "text/plain; charset=utf-8",
        .body = std::move(body),
        .headers = {},
    };
}

Response Response::json(std::string body, int status) {
    return Response{
        .status = status,
        .content_type = "application/json",
        .body = std::move(body),
        .headers = {},
    };
}

Result<HttpServer> HttpServer::create(ServerOptions options) {
    if (options.port < 0 || options.port > 65535) {
        return std::unexpected(Error{"invalid_port", "server port must be between 0 and 65535"});
    }
    if (options.local_access) {
        if (options.host != "127.0.0.1") {
            return std::unexpected(Error{"unsafe_bind", "local access requires host 127.0.0.1"});
        }
        if (!token_component_valid(options.local_access->token)) {
            return std::unexpected(
                Error{"invalid_capability", "local access requires a URL-safe capability token"});
        }
        if (options.local_access->cookie_name.empty() || options.local_access->query_parameter.empty()) {
            return std::unexpected(
                Error{"invalid_local_access", "local access requires cookie and query parameter names"});
        }
    }

    auto implementation = std::make_unique<Impl>();
    implementation->server->set_payload_max_length(options.max_body_bytes);
    implementation->server->set_default_headers({
        {"Cache-Control", "no-store"},
        {"Content-Security-Policy", options.content_security_policy},
        {"Referrer-Policy", "no-referrer"},
        {"X-Content-Type-Options", "nosniff"},
        {"X-Frame-Options", "DENY"},
    });
    if (options.local_access) {
        auto launch_claimed = std::make_shared<std::atomic_bool>(false);
        implementation->server->set_pre_routing_handler([options, launch_claimed](const auto& request, auto& response) {
            return enforce_local_access(options, *launch_claimed, request, response);
        });
    }
    return HttpServer{std::move(options), std::move(implementation)};
}

HttpServer::HttpServer(ServerOptions options, std::unique_ptr<Impl> impl)
    : options_{std::move(options)}, impl_{std::move(impl)} {}

HttpServer::~HttpServer() {
    stop();
}

HttpServer::HttpServer(HttpServer&&) noexcept = default;
HttpServer& HttpServer::operator=(HttpServer&&) noexcept = default;

RunningHttpServer::~RunningHttpServer() = default;

RunningHttpServer::RunningHttpServer(RunningHttpServer&&) noexcept = default;
RunningHttpServer& RunningHttpServer::operator=(RunningHttpServer&&) noexcept = default;

Result<void> RunningHttpServer::wait() {
    if (!impl_) return std::unexpected(Error{"server_state", "HTTP server is not running"});
    if (impl_->thread.joinable()) impl_->thread.join();
    std::scoped_lock lock{impl_->mutex};
    if (!impl_->result) return std::unexpected(Error{"server_state", "HTTP server result is unavailable"});
    return std::move(*impl_->result);
}

void HttpServer::get(std::string path, Handler handler) {
    impl_->server->Get(std::move(path), [handler = std::move(handler)](const auto& request, auto& response) {
        apply_response(response, handler(make_request(request)));
    });
}

void HttpServer::post(std::string path, Handler handler) {
    impl_->server->Post(std::move(path), [handler = std::move(handler)](const auto& request, auto& response) {
        apply_response(response, handler(make_request(request)));
    });
}

void HttpServer::websocket(std::string path, WebSocketHandler handler, WebSocketOptions options) {
    impl_->server->WebSocket(
        std::move(path),
        [handler = std::move(handler), options](const auto& request, auto& socket) {
            auto session =
                WebSocket(std::make_unique<WebSocket::Impl>(WebSocket::Impl{.socket = &socket, .options = options}));
            handler(make_request(request), session);
        });
}

Result<void> HttpServer::serve_static(const std::filesystem::path& root) {
    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) {
        return std::unexpected(Error{"invalid_assets", "static asset directory does not exist: " + root.string()});
    }
    if (!impl_->server->set_mount_point("/", root.string())) {
        return std::unexpected(Error{"invalid_assets", "failed to mount static asset directory: " + root.string()});
    }
    return {};
}

Result<void> HttpServer::serve_static(vfs::FileSystem filesystem) {
    impl_->server->Get(R"(/.*)", [filesystem = std::move(filesystem)](const auto& request, auto& response) {
        const auto path = request.path == "/" ? std::string_view{"index.html"}
                                                : std::string_view{request.path}.substr(1);
        apply_response(response, virtual_file_response(filesystem, path));
    });
    return {};
}

Result<void> HttpServer::serve_files(std::span<const StaticFile> files) {
    std::set<std::string, std::less<>> routes;
    for (const auto& file : files) {
        if (file.route.empty() || file.route.front() != '/' || file.route.find_first_of("?#") != std::string::npos ||
            !routes.emplace(file.route).second) {
            return std::unexpected(Error{"invalid_assets", "development asset routes must be unique absolute paths"});
        }
        std::error_code error;
        if (!std::filesystem::is_regular_file(file.path, error)) {
            return std::unexpected(
                Error{"invalid_assets", "development asset does not exist: " + file.path.string()});
        }
        if (file.content_type.empty()) {
            return std::unexpected(Error{"invalid_assets", "development asset content type must not be empty"});
        }
        impl_->server->Get(file.route, [file](const auto&, auto& response) {
            constexpr std::uintmax_t MAX_DEVELOPMENT_ASSET_BYTES = 64U * 1024U * 1024U;
            std::error_code size_error;
            const auto size = std::filesystem::file_size(file.path, size_error);
            if (size_error || size > MAX_DEVELOPMENT_ASSET_BYTES) {
                response.status = size_error ? 404 : 413;
                return;
            }
            std::ifstream input(file.path, std::ios::binary);
            if (!input) {
                response.status = 404;
                return;
            }
            std::string body(size, '\0');
            input.read(body.data(), static_cast<std::streamsize>(body.size()));
            if (!input && !body.empty()) {
                response.status = 500;
                return;
            }
            response.set_content(std::move(body), file.content_type);
        });
    }
    return {};
}

void HttpServer::serve_file(std::string route, vfs::FileSystem filesystem, std::string path) {
    impl_->server->Get(std::move(route),
                      [filesystem = std::move(filesystem), path = std::move(path)](const auto&, auto& response) {
                          apply_response(response, virtual_file_response(filesystem, path));
                      });
}

Result<int> HttpServer::bind() {
    if (options_.port > 0) {
        if (impl_->server->bind_to_port(options_.host, options_.port)) return options_.port;
        return std::unexpected(Error{"bind_failed", "failed to bind HTTP server"});
    }
    const auto port = impl_->server->bind_to_any_port(options_.host);
    if (port <= 0) return std::unexpected(Error{"bind_failed", "failed to bind HTTP server"});
    return port;
}

Result<RunningHttpServer> HttpServer::start() {
    try {
        auto running = RunningHttpServer{std::make_unique<RunningHttpServer::Impl>(impl_->server)};
        wait_until_ready();
        return running;
    } catch (const std::exception& exception) {
        return std::unexpected(Error{"listen_failed", exception.what()});
    }
}

Result<void> HttpServer::listen_after_bind() {
    if (!impl_->server->listen_after_bind()) {
        return std::unexpected(Error{"listen_failed", "HTTP server stopped with an error"});
    }
    return {};
}

Result<void> HttpServer::listen() {
    if (!impl_->server->listen(options_.host, options_.port)) {
        return std::unexpected(
            Error{"listen_failed", "failed to listen on " + options_.host + ':' + std::to_string(options_.port)});
    }
    return {};
}

void HttpServer::stop() {
    if (impl_) {
        impl_->server->stop();
    }
}

void HttpServer::wait_until_ready() const {
    impl_->server->wait_until_ready();
}

std::string HttpServer::local_launch_url(int port) const {
    if (port <= 0 || port > 65535) return {};
    auto result = "http://" + options_.host + ':' + std::to_string(port) + '/';
    if (options_.local_access)
        result += '?' + options_.local_access->query_parameter + '=' + options_.local_access->token;
    return result;
}

}  // namespace liteapp