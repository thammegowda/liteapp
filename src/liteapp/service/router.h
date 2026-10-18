#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "liteapp/core/result.h"
#include "liteapp/http/server.h"

namespace liteapp {

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
    [[nodiscard]] std::stop_token stop_token() const noexcept;
    [[nodiscard]] bool emit(std::string_view type, nlohmann::json payload = nlohmann::json::object()) const;
    [[nodiscard]] bool fail(Error error) const;
    [[nodiscard]] bool send_binary(std::span<const std::byte> data) const;

private:
    friend class ServiceRouter;
    struct Impl;

    explicit ServiceContext(std::shared_ptr<Impl> impl);

    std::shared_ptr<Impl> impl_;
};

using ServiceHandler = std::function<Result<nlohmann::json>(ServiceContext&)>;
using BinaryServiceHandler = std::function<void(std::span<const std::byte>)>;

struct ServiceRouterOptions {
    std::size_t max_active_requests{16};
    std::size_t max_id_bytes{128};
    std::size_t max_method_bytes{128};
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
    void set_fallback(ServiceHandler handler);
    void set_binary_handler(BinaryServiceHandler handler);
    void run(WebSocket& socket) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace liteapp
