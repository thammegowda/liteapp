#include "liteapp/fs/watcher.h"

#include "liteapp/fs/watcher_backend.h"

namespace liteapp::fs {
struct Watcher::Impl { std::unique_ptr<WatcherBackend> backend; };

Result<Watcher> Watcher::create(
    std::span<const std::filesystem::path> roots,
    Callback callback) {
    if (!callback) return std::unexpected(Error{"watcher", "watch callback is required"});
    if (roots.empty()) return std::unexpected(Error{"watcher", "at least one watch root is required"});
    auto backend = create_native_watcher(roots, std::move(callback));
    if (!backend) return std::unexpected(backend.error());
    auto impl = std::make_unique<Impl>();
    impl->backend = std::move(*backend);
    return Watcher{std::move(impl)};
}

Watcher::Watcher(std::unique_ptr<Impl> impl) : impl_{std::move(impl)} {}
Watcher::~Watcher() = default;
Watcher::Watcher(Watcher&&) noexcept = default;
Watcher& Watcher::operator=(Watcher&&) noexcept = default;

}  // namespace liteapp::fs