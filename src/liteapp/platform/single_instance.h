#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include "liteapp/core/result.h"

namespace liteapp::platform {

class SingleInstance {
public:
    [[nodiscard]] static Result<std::optional<SingleInstance>> acquire(std::string_view app_id);

    ~SingleInstance();
    SingleInstance(SingleInstance&&) noexcept;
    SingleInstance& operator=(SingleInstance&&) noexcept;
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    void set_activation_handler(std::function<void()> handler);
    void clear_activation_handler();

private:
    struct Impl;

    explicit SingleInstance(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace liteapp::platform
