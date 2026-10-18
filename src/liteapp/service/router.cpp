#include "liteapp/service/router.h"

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
    std::atomic_bool done{false};
    std::atomic_bool terminal{false};
    std::jthread thread;
};

struct Connection {
    explicit Connection(WebSocket& value) : socket{value} {}

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
        if (disconnected.load() || !socket.is_open()) return false;
        if (!socket.send_text(message.dump())) {
            disconnected.store(true);
            return false;
        }
        return true;
    }

    bool send_binary(std::span<const std::byte> data) {
        std::scoped_lock lock{send_mutex};
        if (disconnected.load() || !socket.is_open()) return false;
        if (!socket.send_binary(data)) {
            disconnected.store(true);
            return false;
        }
        return true;
    }

    WebSocket& socket;
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
}  // namespace

struct ServiceContext::Impl {
    std::string id;
    std::string method;
    Json payload;
    std::shared_ptr<Connection> connection;
    RequestState* request;
    std::stop_token stop;
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

std::stop_token ServiceContext::stop_token() const noexcept {
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
    bool expected = false;
    if (!impl_->request->terminal.compare_exchange_strong(expected, true)) return false;
    impl_->request->thread.request_stop();
    return impl_->connection->send_event(
        *impl_->request, "error",
        {{"code", std::move(error.code)}, {"message", std::move(error.message)}});
}

struct ServiceRouter::Impl {
    explicit Impl(ServiceRouterOptions configured) : options{configured} {
        if (!options.max_active_requests || !options.max_id_bytes || !options.max_method_bytes)
            throw std::invalid_argument("service router limits must be positive");
    }

    ServiceRouterOptions options;
    std::unordered_map<std::string, ServiceHandler> handlers;
    ServiceHandler fallback;
    BinaryServiceHandler binary;
};

ServiceRouter::ServiceRouter(ServiceRouterOptions options) : impl_{std::make_unique<Impl>(options)} {}
ServiceRouter::~ServiceRouter() = default;
ServiceRouter::ServiceRouter(ServiceRouter&&) noexcept = default;
ServiceRouter& ServiceRouter::operator=(ServiceRouter&&) noexcept = default;

void ServiceRouter::register_method(std::string method, ServiceHandler handler) {
    if (method.empty() || method.size() > impl_->options.max_method_bytes || !handler)
        throw std::invalid_argument("service method and handler must be valid");
    if (!impl_->handlers.emplace(std::move(method), std::move(handler)).second)
        throw std::invalid_argument("service method is already registered");
}

void ServiceRouter::set_fallback(ServiceHandler handler) {
    impl_->fallback = std::move(handler);
}

void ServiceRouter::set_binary_handler(BinaryServiceHandler handler) {
    impl_->binary = std::move(handler);
}

void ServiceRouter::run(WebSocket& socket) const {
    auto connection = std::make_shared<Connection>(socket);
    std::unordered_map<std::string, std::unique_ptr<RequestState>> active;
    const auto reap = [&] {
        for (auto iterator = active.begin(); iterator != active.end();) {
            if (!iterator->second->done.load()) {
                ++iterator;
                continue;
            }
            if (iterator->second->thread.joinable()) iterator->second->thread.join();
            iterator = active.erase(iterator);
        }
    };
    while (socket.is_open()) {
        reap();
        auto received = socket.read();
        if (!received || !*received) break;
        if ((**received).kind == WebSocketMessageKind::binary) {
            if (impl_->binary) {
                const auto& data = (**received).data;
                impl_->binary(std::as_bytes(std::span{data.data(), data.size()}));
            }
            continue;
        }
        const auto message = Json::parse((**received).data, nullptr, false);
        if (message.is_discarded() || !message.is_object() || message.value("version", 0) != 1 ||
            !valid_component(message.value("id", Json{}), impl_->options.max_id_bytes) ||
            !message.contains("kind") || !message["kind"].is_string()) {
            socket.close("invalid service envelope");
            break;
        }
        const auto id = message["id"].get<std::string>();
        const auto kind = message["kind"].get<std::string>();
        if (kind == "cancel") {
            if (const auto found = active.find(id); found != active.end()) found->second->thread.request_stop();
            continue;
        }
        if (kind != "request" ||
            !valid_component(message.value("method", Json{}), impl_->options.max_method_bytes)) {
            socket.close("invalid service request");
            break;
        }
        if (active.contains(id)) {
            RequestState duplicate;
            duplicate.id = id;
            connection->send_event(
                duplicate, "error",
                {{"code", "duplicate_request"}, {"message", "Request ID is already active"}});
            continue;
        }
        if (active.size() >= impl_->options.max_active_requests) {
            RequestState saturated;
            saturated.id = id;
            connection->send_event(
                saturated, "error",
                {{"code", "too_many_requests"}, {"message", "Too many service requests are active"}});
            continue;
        }
        const auto method = message["method"].get<std::string>();
        const auto found = impl_->handlers.find(method);
        auto handler = found == impl_->handlers.end() ? impl_->fallback : found->second;
        if (!handler) {
            RequestState unknown;
            unknown.id = id;
            connection->send_event(
                unknown, "error",
                {{"code", "unknown_method"}, {"message", "Unknown service method: " + method}});
            continue;
        }
        auto request = std::make_unique<RequestState>();
        request->id = id;
        auto* state = request.get();
        const auto payload = message.value("payload", Json{});
        state->thread = std::jthread(
            [connection, state, id, method, payload, handler = std::move(handler)](std::stop_token stop) mutable {
                auto context = ServiceContext{std::make_shared<ServiceContext::Impl>(
                    ServiceContext::Impl{
                        .id = id,
                        .method = method,
                        .payload = payload,
                        .connection = connection,
                        .request = state,
                        .stop = stop,
                    })};
                Result<Json> result;
                try {
                    result = handler(context);
                } catch (const ServiceFailure& error) {
                    result = std::unexpected(Error{error.code, error.what()});
                } catch (const std::exception& error) {
                    result = std::unexpected(Error{"service_error", error.what()});
                } catch (...) {
                    result = std::unexpected(Error{"service_error", "service handler failed"});
                }
                bool expected = false;
                if (state->terminal.compare_exchange_strong(expected, true)) {
                    if (context.cancelled()) {
                        connection->send_event(*state, "cancelled", {{"message", "Request cancelled"}});
                    } else if (result) {
                        connection->send_event(*state, "completed", std::move(*result));
                    } else {
                        connection->send_event(
                            *state, "error",
                            {{"code", result.error().code}, {"message", result.error().message}});
                    }
                }
                state->done.store(true);
            });
        active.emplace(id, std::move(request));
    }
    connection->disconnected.store(true);
    for (auto& [id, request] : active) {
        static_cast<void>(id);
        request->thread.request_stop();
    }
    for (auto& [id, request] : active) {
        static_cast<void>(id);
        if (request->thread.joinable()) request->thread.join();
    }
}

}  // namespace liteapp
