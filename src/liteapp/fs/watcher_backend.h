#pragma once

#include <memory>
#include <span>

#include "liteapp/fs/watcher.h"

namespace liteapp::fs {

class WatcherBackend {
public:
    virtual ~WatcherBackend() = default;
};

[[nodiscard]] Result<std::unique_ptr<WatcherBackend>> create_native_watcher(
    std::span<const std::filesystem::path> roots,
    Callback callback);

inline void notify(const Callback& callback, const std::filesystem::path& path, Change change) noexcept {
    try { callback(path, change); } catch (...) {}
}

}  // namespace liteapp::fs