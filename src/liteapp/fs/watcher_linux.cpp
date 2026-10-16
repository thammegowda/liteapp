#include "liteapp/fs/watcher_backend.h"

#if defined(__linux__)

#include <array>
#include <cerrno>
#include <cstdint>
#include <map>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <thread>
#include <unistd.h>

namespace liteapp::fs {
namespace {

class FileDescriptor {
public:
    explicit FileDescriptor(int value = -1) : value_{value} {}
    ~FileDescriptor() { if (value_ >= 0) ::close(value_); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    [[nodiscard]] int get() const noexcept { return value_; }

private:
    int value_;
};

class InotifyBackend final : public WatcherBackend {
public:
    static Result<std::unique_ptr<WatcherBackend>> create(
        std::span<const std::filesystem::path> roots,
        Callback callback) {
        auto backend = std::unique_ptr<InotifyBackend>{new InotifyBackend{std::move(callback)}};
        if (backend->inotify_.get() < 0 || backend->stop_.get() < 0) {
            return std::unexpected(Error{"watcher", "failed to initialize inotify"});
        }
        for (const auto& root : roots) {
            if (backend->add_tree(root)) backend->roots_.push_back(root);
        }
        if (backend->roots_.empty()) return std::unexpected(Error{"watcher", "no roots support inotify"});
        backend->worker_ = std::jthread{[self = backend.get()] { self->run(); }};
        return backend;
    }

    ~InotifyBackend() override {
        const std::uint64_t signal = 1;
        while (::write(stop_.get(), &signal, sizeof(signal)) < 0 && errno == EINTR) {}
    }

private:
    static constexpr std::uint32_t mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE |
                                          IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB | IN_DELETE_SELF |
                                          IN_MOVE_SELF | IN_Q_OVERFLOW;

    explicit InotifyBackend(Callback callback)
        : callback_{std::move(callback)},
          inotify_{::inotify_init1(IN_CLOEXEC | IN_NONBLOCK)},
          stop_{::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)} {}

    bool add_directory(const std::filesystem::path& path) {
        const auto descriptor = ::inotify_add_watch(inotify_.get(), path.c_str(), mask);
        if (descriptor < 0) return false;
        directories_[descriptor] = path;
        return true;
    }

    bool add_tree(const std::filesystem::path& root) {
        std::error_code error;
        if (!std::filesystem::is_directory(root, error) || std::filesystem::is_symlink(root, error)) return false;
        if (!add_directory(root)) return false;
        std::filesystem::recursive_directory_iterator entries{
            root, std::filesystem::directory_options::skip_permission_denied, error};
        for (auto end = std::filesystem::recursive_directory_iterator{}; entries != end; entries.increment(error)) {
            if (error) { error.clear(); continue; }
            if (entries->is_symlink(error)) { entries.disable_recursion_pending(); error.clear(); continue; }
            if (entries->is_directory(error)) static_cast<void>(add_directory(entries->path()));
            error.clear();
        }
        return true;
    }

    void run() {
        std::array<pollfd, 2> descriptors{{{inotify_.get(), POLLIN, 0}, {stop_.get(), POLLIN, 0}}};
        alignas(inotify_event) std::array<std::byte, 64U * 1024U> buffer{};
        while (true) {
            const auto ready = ::poll(descriptors.data(), descriptors.size(), -1);
            if (ready < 0) { if (errno == EINTR) continue; break; }
            if ((descriptors[1].revents & POLLIN) != 0) break;
            if ((descriptors[0].revents & POLLIN) == 0) continue;
            const auto length = ::read(inotify_.get(), buffer.data(), buffer.size());
            if (length < 0) { if (errno == EINTR || errno == EAGAIN) continue; break; }
            handle(buffer.data(), static_cast<std::size_t>(length));
        }
    }

    void handle(const std::byte* bytes, std::size_t length) {
        std::size_t offset = 0;
        while (offset + sizeof(inotify_event) <= length) {
            const auto* event = reinterpret_cast<const inotify_event*>(bytes + offset);
            const auto size = sizeof(inotify_event) + event->len;
            if (offset + size > length) break;
            if ((event->mask & IN_Q_OVERFLOW) != 0) {
                for (const auto& root : roots_) notify(callback_, root, Change::rescan);
            } else if (const auto found = directories_.find(event->wd); found != directories_.end()) {
                const auto path = event->len ? found->second / event->name : found->second;
                if ((event->mask & IN_ISDIR) != 0 && (event->mask & (IN_CREATE | IN_MOVED_TO)) != 0) add_tree(path);
                Change action = Change::modified;
                if ((event->mask & IN_CREATE) != 0) action = Change::added;
                else if ((event->mask & IN_DELETE) != 0) action = Change::removed;
                else if ((event->mask & (IN_MOVED_FROM | IN_MOVED_TO)) != 0) action = Change::moved;
                notify(callback_, path, action);
                if ((event->mask & IN_IGNORED) != 0) directories_.erase(found);
            }
            offset += size;
        }
    }

    Callback callback_;
    FileDescriptor inotify_;
    FileDescriptor stop_;
    std::map<int, std::filesystem::path> directories_;
    std::vector<std::filesystem::path> roots_;
    std::jthread worker_;
};

}  // namespace

Result<std::unique_ptr<WatcherBackend>> create_native_watcher(
    std::span<const std::filesystem::path> roots,
    Callback callback) {
    return InotifyBackend::create(roots, std::move(callback));
}

}  // namespace liteapp::fs

#endif