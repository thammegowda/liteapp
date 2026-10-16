#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <span>

#include "liteapp/core/result.h"

namespace liteapp::fs {

enum class Change { added, removed, modified, moved, rescan };
using Callback = std::function<void(const std::filesystem::path&, Change)>;

class Watcher {
public:
    [[nodiscard]] static Result<Watcher> create(
        std::span<const std::filesystem::path> roots,
        Callback callback);

    ~Watcher();
    Watcher(Watcher&&) noexcept;
    Watcher& operator=(Watcher&&) noexcept;
    Watcher(const Watcher&) = delete;
    Watcher& operator=(const Watcher&) = delete;

private:
    struct Impl;
    explicit Watcher(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace liteapp::fs