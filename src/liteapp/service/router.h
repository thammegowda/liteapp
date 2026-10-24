#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "liteapp/core/result.h"
namespace liteapp {

class WebSocket;

class ServiceStopToken {
public:
    ServiceStopToken() = default;
    explicit ServiceStopToken(std::shared_ptr<const std::atomic_bool> stopped)
        : stopped_{std::move(stopped)} {}

    [[nodiscard]] bool stop_requested() const noexcept {
        return stopped_ && stopped_->load();
    }

private:
    std::shared_ptr<const std::atomic_bool> stopped_;
};

class ServiceFailure : public std::runtime_error {
public:
    explicit ServiceFailure(Error error)
        : std::runtime_error{error.message}, code{std::move(error.code)} {}

    std::string code;
};

class ServiceContext {
public:
    [[nodiscard]] std::string_view id() const noexcept;
    [[nodiscard]] std::string_view method() const noexcept;
    [[nodiscard]] const nlohmann::json& payload() const noexcept;
    [[nodiscard]] bool cancelled() const noexcept;
    [[nodiscard]] ServiceStopToken stop_token() const noexcept;
    [[nodiscard]] bool emit(std::string_view type, nlohmann::json payload = nlohmann::json::object()) const;
    [[nodiscard]] bool fail(Error error) const;
    [[nodiscard]] bool send_binary(std::span<const std::byte> data) const;

private:
    friend class ServiceResponder;
    friend class ServiceRouter;
    friend class ServiceSession;
    struct Impl;

    explicit ServiceContext(std::shared_ptr<Impl> impl);

    std::shared_ptr<Impl> impl_;
};

class ServiceResponder {
public:
    struct Impl;

    ServiceResponder() = default;
    explicit ServiceResponder(std::shared_ptr<Impl> impl);
    ~ServiceResponder();
    ServiceResponder(ServiceResponder&&) noexcept;
    ServiceResponder& operator=(ServiceResponder&&) noexcept;
    ServiceResponder(const ServiceResponder&) = delete;
    ServiceResponder& operator=(const ServiceResponder&) = delete;

    [[nodiscard]] bool cancelled() const noexcept;
    [[nodiscard]] ServiceStopToken stop_token() const noexcept;
    [[nodiscard]] bool emit(std::string_view type, nlohmann::json payload = nlohmann::json::object()) const;
    [[nodiscard]] bool send_binary(std::span<const std::byte> data) const;
    [[nodiscard]] bool complete(nlohmann::json payload = nlohmann::json::object());
    [[nodiscard]] bool fail(Error error);

private:
    std::shared_ptr<Impl> impl_;
};

using ServiceHandler = std::function<Result<nlohmann::json>(ServiceContext&)>;
using AsyncServiceHandler = std::function<void(ServiceContext&, ServiceResponder)>;
using BinaryServiceHandler = std::function<void(std::span<const std::byte>)>;

struct ServiceRouterOptions {
    std::size_t max_active_requests{16};
    std::size_t max_id_bytes{128};
    std::size_t max_method_bytes{128};
};

struct ServiceSessionCallbacks {
    std::function<bool(std::string_view)> send_text;
    std::function<bool(std::span<const std::byte>)> send_binary;
    std::function<void(std::string_view)> close;
};

class ServiceSession {
public:
    ~ServiceSession();
    ServiceSession(ServiceSession&&) noexcept;
    ServiceSession& operator=(ServiceSession&&) noexcept;
    ServiceSession(const ServiceSession&) = delete;
    ServiceSession& operator=(const ServiceSession&) = delete;

    [[nodiscard]] bool receive_text(std::string_view message);
    [[nodiscard]] bool receive_binary(std::span<const std::byte> data);
    void close();

private:
    friend class ServiceRouter;
    struct Impl;

    explicit ServiceSession(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

class ServiceRouter {
public:
    explicit ServiceRouter(ServiceRouterOptions options = {});
    ~ServiceRouter();
    ServiceRouter(ServiceRouter&&) noexcept;
    ServiceRouter& operator=(ServiceRouter&&) noexcept;
    ServiceRouter(const ServiceRouter&) = delete;
    ServiceRouter& operator=(const ServiceRouter&) = delete;

    void register_method(std::string method, ServiceHandler handler);
    void register_async_method(std::string method, AsyncServiceHandler handler);
    void set_fallback(ServiceHandler handler);
    void set_binary_handler(BinaryServiceHandler handler);
    [[nodiscard]] std::unique_ptr<ServiceSession> open(ServiceSessionCallbacks callbacks) const;
    void run(WebSocket& socket) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace liteapp
