#include "liteapp/service/router.h"

#ifndef LITEAPP_IN_PROCESS_ONLY
#include "liteapp/http/server.h"
#endif

#include <atomic>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace liteapp {
namespace {
using Json = nlohmann::json;

struct RequestState {
    std::string id;
    std::atomic_size_t sequence{0};
    std::atomic_bool invocation_done{false};
    std::atomic_bool terminal{false};
    std::shared_ptr<std::atomic_bool> stopped{std::make_shared<std::atomic_bool>(false)};
    std::thread thread;

    void request_stop() const noexcept { stopped->store(true); }
};

struct Connection {
    explicit Connection(ServiceSessionCallbacks value) : callbacks{std::move(value)} {}

    bool send_event(RequestState& request, std::string_view type, Json payload) {
        if (disconnected.load()) return false;
        const auto sequence = request.sequence.fetch_add(1);
        const auto message = Json{
            {"version", 1},
            {"kind", "event"},
            {"id", request.id},
            {"sequence", sequence},
            {"type", type},
            {"payload", std::move(payload)},
        };
        std::scoped_lock lock{send_mutex};
        if (disconnected.load()) return false;
        try {
            if (callbacks.send_text(message.dump())) return true;
        } catch (...) {
        }
        disconnected.store(true);
        return false;
    }

    bool send_binary(std::span<const std::byte> data) {
        std::scoped_lock lock{send_mutex};
        if (disconnected.load() || !callbacks.send_binary) return false;
        try {
            if (callbacks.send_binary(data)) return true;
        } catch (...) {
        }
        disconnected.store(true);
        return false;
    }

    void disconnect(std::string_view reason) {
        if (disconnected.exchange(true)) return;
        if (callbacks.close) callbacks.close(reason);
    }

    ServiceSessionCallbacks callbacks;
    std::mutex send_mutex;
    std::atomic_bool disconnected{false};
};

bool reserved_event(std::string_view type) {
    return type == "completed" || type == "cancelled" || type == "error";
}

bool valid_component(const Json& value, std::size_t maximum) {
    return value.is_string() && !value.get_ref<const std::string&>().empty() &&
           value.get_ref<const std::string&>().size() <= maximum;
}

bool finish_request(const std::shared_ptr<Connection>& connection,
                    const std::shared_ptr<RequestState>& request,
                    std::string_view type,
                    Json payload) {
    bool expected = false;
    if (!request->terminal.compare_exchange_strong(expected, true)) return false;
    if (type != "completed") request->request_stop();
    return connection->send_event(*request, type, std::move(payload));
}
}  // namespace

struct ServiceContext::Impl {
    std::string id;
    std::string method;
    Json payload;
    std::shared_ptr<Connection> connection;
    std::shared_ptr<RequestState> request;
    ServiceStopToken stop;
};

ServiceContext::ServiceContext(std::shared_ptr<Impl> impl) : impl_{std::move(impl)} {}

std::string_view ServiceContext::id() const noexcept {
    return impl_->id;
}

std::string_view ServiceContext::method() const noexcept {
    return impl_->method;
}

const Json& ServiceContext::payload() const noexcept {
    return impl_->payload;
}

bool ServiceContext::cancelled() const noexcept {
    return impl_->stop.stop_requested() || impl_->connection->disconnected.load();
}

ServiceStopToken ServiceContext::stop_token() const noexcept {
    return impl_->stop;
}

bool ServiceContext::emit(std::string_view type, Json payload) const {
    if (type.empty() || type.size() > 128 || reserved_event(type))
        throw std::invalid_argument("service event type is invalid or reserved");
    if (cancelled()) return false;
    return impl_->connection->send_event(*impl_->request, type, std::move(payload));
}

bool ServiceContext::send_binary(std::span<const std::byte> data) const {
    if (cancelled()) return false;
    return impl_->connection->send_binary(data);
}

bool ServiceContext::fail(Error error) const {
    return finish_request(
        impl_->connection, impl_->request, "error",
        {{"code", std::move(error.code)}, {"message", std::move(error.message)}});
}

struct ServiceResponder::Impl {
    explicit Impl(std::shared_ptr<ServiceContext::Impl> value) : context{std::move(value)} {}

    ~Impl() {
        if (!context || context->request->terminal.load()) return;
        if (context->stop.stop_requested() || context->connection->disconnected.load())
            static_cast<void>(finish_request(
                context->connection, context->request, "cancelled", {{"message", "Request cancelled"}}));
        else
            static_cast<void>(finish_request(
                context->connection, context->request, "error",
                {{"code", "incomplete_async_request"},
                 {"message", "Asynchronous service handler ended without completing the request"}}));
    }

    std::shared_ptr<ServiceContext::Impl> context;
};

ServiceResponder::ServiceResponder(std::shared_ptr<Impl> impl) : impl_{std::move(impl)} {}
ServiceResponder::~ServiceResponder() = default;
ServiceResponder::ServiceResponder(ServiceResponder&&) noexcept = default;
ServiceResponder& ServiceResponder::operator=(ServiceResponder&&) noexcept = default;

bool ServiceResponder::cancelled() const noexcept {
    return !impl_ || impl_->context->stop.stop_requested() || impl_->context->connection->disconnected.load();
}

ServiceStopToken ServiceResponder::stop_token() const noexcept {
    return impl_ ? impl_->context->stop : ServiceStopToken{};
}

bool ServiceResponder::emit(std::string_view type, Json payload) const {
    if (!impl_) return false;
    auto context = ServiceContext{impl_->context};
    return context.emit(type, std::move(payload));
}

bool ServiceResponder::send_binary(std::span<const std::byte> data) const {
    if (!impl_) return false;
    auto context = ServiceContext{impl_->context};
    return context.send_binary(data);
}

bool ServiceResponder::complete(Json payload) {
    if (!impl_) return false;
    const auto completed = finish_request(
        impl_->context->connection, impl_->context->request, "completed", std::move(payload));
    impl_.reset();
    return completed;
}

bool ServiceResponder::fail(Error error) {
    if (!impl_) return false;
    const auto failed = finish_request(
        impl_->context->connection, impl_->context->request, "error",
        {{"code", std::move(error.code)}, {"message", std::move(error.message)}});
    impl_.reset();
    return failed;
}

struct ServiceHandlerEntry {
    ServiceHandler synchronous;
    AsyncServiceHandler asynchronous;
};

struct ServiceRouter::Impl {
    explicit Impl(ServiceRouterOptions configured) : options{configured} {
        if (!options.max_active_requests || !options.max_id_bytes || !options.max_method_bytes)
            throw std::invalid_argument("service router limits must be positive");
    }

    ServiceRouterOptions options;
    std::unordered_map<std::string, ServiceHandlerEntry> handlers;
    ServiceHandler fallback;
    BinaryServiceHandler binary;
};

struct ServiceSession::Impl {
    explicit Impl(ServiceRouterOptions configured_options,
                  std::unordered_map<std::string, ServiceHandlerEntry> configured_handlers,
                  ServiceHandler configured_fallback,
                  BinaryServiceHandler configured_binary,
                  ServiceSessionCallbacks configured)
        : options{configured_options},
          handlers{std::move(configured_handlers)},
          fallback{std::move(configured_fallback)},
          binary{std::move(configured_binary)},
          connection{std::make_shared<Connection>(std::move(configured))} {}

    ~Impl() { close(); }

    void reap_locked() {
        for (auto iterator = active.begin(); iterator != active.end();) {
            if (!iterator->second->invocation_done.load()) {
                ++iterator;
                continue;
            }
            if (iterator->second->thread.joinable()) iterator->second->thread.join();
            if (iterator->second->terminal.load())
                iterator = active.erase(iterator);
            else
                ++iterator;
        }
    }

    bool receive_text(std::string_view source) {
        if (connection->disconnected.load()) return false;
        const auto message = Json::parse(source, nullptr, false);
        if (message.is_discarded() || !message.is_object() || message.value("version", 0) != 1 ||
            !valid_component(message.value("id", Json{}), options.max_id_bytes) ||
            !message.contains("kind") || !message["kind"].is_string()) {
            connection->disconnect("invalid service envelope");
            return false;
        }
        const auto id = message["id"].get<std::string>();
        const auto kind = message["kind"].get<std::string>();
        std::scoped_lock lock{active_mutex};
        reap_locked();
        if (kind == "cancel") {
            if (const auto found = active.find(id); found != active.end())
                static_cast<void>(finish_request(
                    connection, found->second, "cancelled", {{"message", "Request cancelled"}}));
            return true;
        }
        if (kind != "request" ||
            !valid_component(message.value("method", Json{}), options.max_method_bytes)) {
            connection->disconnect("invalid service request");
            return false;
        }
        if (active.contains(id)) {
            RequestState duplicate;
            duplicate.id = id;
            connection->send_event(
                duplicate, "error",
                {{"code", "duplicate_request"}, {"message", "Request ID is already active"}});
            return true;
        }
        if (active.size() >= options.max_active_requests) {
            RequestState saturated;
            saturated.id = id;
            connection->send_event(
                saturated, "error",
                {{"code", "too_many_requests"}, {"message", "Too many service requests are active"}});
            return true;
        }
        const auto method = message["method"].get<std::string>();
        const auto found = handlers.find(method);
        auto entry = found == handlers.end()
            ? ServiceHandlerEntry{.synchronous = fallback, .asynchronous = {}}
            : found->second;
        if (!entry.synchronous && !entry.asynchronous) {
            RequestState unknown;
            unknown.id = id;
            connection->send_event(
                unknown, "error",
                {{"code", "unknown_method"}, {"message", "Unknown service method: " + method}});
            return true;
        }
        auto request = std::make_shared<RequestState>();
        request->id = id;
        const auto payload = message.value("payload", Json{});
        request->thread = std::thread(
            [connection = connection, request, id, method, payload, entry = std::move(entry)]() mutable {
                auto context_impl = std::make_shared<ServiceContext::Impl>(
                    ServiceContext::Impl{
                        .id = id,
                        .method = method,
                        .payload = payload,
                        .connection = connection,
                        .request = request,
                        .stop = ServiceStopToken{request->stopped},
                    });
                auto context = ServiceContext{context_impl};
                if (entry.asynchronous) {
                    auto responder_impl = std::make_shared<ServiceResponder::Impl>(context_impl);
                    try {
                        entry.asynchronous(context, ServiceResponder{responder_impl});
                    } catch (const ServiceFailure& error) {
                        static_cast<void>(finish_request(
                            connection, request, "error", {{"code", error.code}, {"message", error.what()}}));
                    } catch (const std::exception& error) {
                        static_cast<void>(finish_request(
                            connection, request, "error",
                            {{"code", "service_error"}, {"message", error.what()}}));
                    } catch (...) {
                        static_cast<void>(finish_request(
                            connection, request, "error",
                            {{"code", "service_error"}, {"message", "service handler failed"}}));
                    }
                    request->invocation_done.store(true);
                    responder_impl.reset();
                    return;
                }
                Result<Json> result;
                try {
                    result = entry.synchronous(context);
                } catch (const ServiceFailure& error) {
                    result = std::unexpected(Error{error.code, error.what()});
                } catch (const std::exception& error) {
                    result = std::unexpected(Error{"service_error", error.what()});
                } catch (...) {
                    result = std::unexpected(Error{"service_error", "service handler failed"});
                }
                if (context.cancelled()) {
                    static_cast<void>(finish_request(
                        connection, request, "cancelled", {{"message", "Request cancelled"}}));
                } else if (result) {
                    static_cast<void>(finish_request(connection, request, "completed", std::move(*result)));
                } else {
                    static_cast<void>(finish_request(
                        connection, request, "error",
                        {{"code", result.error().code}, {"message", result.error().message}}));
                }
                request->invocation_done.store(true);
            });
        active.emplace(id, request);
        return true;
    }

    bool receive_binary(std::span<const std::byte> data) {
        if (connection->disconnected.load()) return false;
        if (!binary) return true;
        try {
            binary(data);
            return true;
        } catch (...) {
            connection->disconnect("binary service handler failed");
            return false;
        }
    }

    void close() {
        connection->disconnect("service session closed");
        std::unordered_map<std::string, std::shared_ptr<RequestState>> closing;
        {
            std::scoped_lock lock{active_mutex};
            closing.swap(active);
        }
        for (auto& [id, request] : closing) {
            static_cast<void>(id);
            request->request_stop();
        }
        for (auto& [id, request] : closing) {
            static_cast<void>(id);
            if (request->thread.joinable()) request->thread.join();
        }
    }

    ServiceRouterOptions options;
    std::unordered_map<std::string, ServiceHandlerEntry> handlers;
    ServiceHandler fallback;
    BinaryServiceHandler binary;
    std::shared_ptr<Connection> connection;
    std::mutex active_mutex;
    std::unordered_map<std::string, std::shared_ptr<RequestState>> active;
};

ServiceSession::ServiceSession(std::unique_ptr<Impl> impl) : impl_{std::move(impl)} {}
ServiceSession::~ServiceSession() = default;
ServiceSession::ServiceSession(ServiceSession&&) noexcept = default;
ServiceSession& ServiceSession::operator=(ServiceSession&&) noexcept = default;

bool ServiceSession::receive_text(std::string_view message) {
    return impl_ && impl_->receive_text(message);
}

bool ServiceSession::receive_binary(std::span<const std::byte> data) {
    return impl_ && impl_->receive_binary(data);
}

void ServiceSession::close() {
    if (impl_) impl_->close();
}

ServiceRouter::ServiceRouter(ServiceRouterOptions options) : impl_{std::make_unique<Impl>(options)} {}
ServiceRouter::~ServiceRouter() = default;
ServiceRouter::ServiceRouter(ServiceRouter&&) noexcept = default;
ServiceRouter& ServiceRouter::operator=(ServiceRouter&&) noexcept = default;

void ServiceRouter::register_method(std::string method, ServiceHandler handler) {
    if (method.empty() || method.size() > impl_->options.max_method_bytes || !handler)
        throw std::invalid_argument("service method and handler must be valid");
    if (!impl_->handlers.emplace(
            std::move(method),
            ServiceHandlerEntry{.synchronous = std::move(handler), .asynchronous = {}}).second)
        throw std::invalid_argument("service method is already registered");
}

void ServiceRouter::register_async_method(std::string method, AsyncServiceHandler handler) {
    if (method.empty() || method.size() > impl_->options.max_method_bytes || !handler)
        throw std::invalid_argument("service method and handler must be valid");
    if (!impl_->handlers.emplace(
            std::move(method),
            ServiceHandlerEntry{.synchronous = {}, .asynchronous = std::move(handler)}).second)
        throw std::invalid_argument("service method is already registered");
}

void ServiceRouter::set_fallback(ServiceHandler handler) {
    impl_->fallback = std::move(handler);
}

void ServiceRouter::set_binary_handler(BinaryServiceHandler handler) {
    impl_->binary = std::move(handler);
}

std::unique_ptr<ServiceSession> ServiceRouter::open(ServiceSessionCallbacks callbacks) const {
    if (!callbacks.send_text) throw std::invalid_argument("service session requires a text sender");
    return std::unique_ptr<ServiceSession>{new ServiceSession{std::make_unique<ServiceSession::Impl>(
        impl_->options, impl_->handlers, impl_->fallback, impl_->binary, std::move(callbacks))}};
}

#ifndef LITEAPP_IN_PROCESS_ONLY
void ServiceRouter::run(WebSocket& socket) const {
    auto session = open({
        .send_text = [&socket](std::string_view message) {
            return socket.is_open() && socket.send_text(message);
        },
        .send_binary = [&socket](std::span<const std::byte> data) {
            return socket.is_open() && socket.send_binary(data);
        },
        .close = [&socket](std::string_view reason) { socket.close(reason); },
    });
    while (socket.is_open()) {
        auto received = socket.read();
        if (!received || !*received) break;
        if ((**received).kind == WebSocketMessageKind::binary) {
            const auto& data = (**received).data;
            if (!session->receive_binary(std::as_bytes(std::span{data.data(), data.size()}))) break;
        } else if (!session->receive_text((**received).data)) {
            break;
        }
    }
    session->close();
}
#endif

}  // namespace liteapp
