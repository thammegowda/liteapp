#include "liteapp/fs/watcher_backend.h"

#if defined(_WIN32)

#include <array>
#include <cstddef>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>

namespace liteapp::fs {
namespace {

class Handle {
public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) : value_{value} {}
    ~Handle() { if (valid()) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_{std::exchange(other.value_, INVALID_HANDLE_VALUE)} {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (valid()) CloseHandle(value_);
            value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    [[nodiscard]] bool valid() const noexcept {
        return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
    }

private:
    HANDLE value_;
};

struct DirectoryWatch {
    Handle directory;
    OVERLAPPED overlapped{};
    alignas(FILE_NOTIFY_INFORMATION) std::array<std::byte, 63U * 1024U> buffer{};
    std::filesystem::path root;
};

class IocpBackend final : public WatcherBackend {
public:
    static Result<std::unique_ptr<WatcherBackend>> create(
        std::span<const std::filesystem::path> roots,
        Callback callback) {
        auto backend = std::unique_ptr<IocpBackend>{new IocpBackend{std::move(callback)}};
        if (!backend->port_.valid()) return std::unexpected(Error{"watcher", "failed to create IOCP"});
        for (const auto& root : roots) backend->add_directory(root);
        if (backend->watches_.empty()) return std::unexpected(Error{"watcher", "no roots support IOCP watching"});
        backend->worker_ = std::jthread{[self = backend.get()] { self->run(); }};
        return backend;
    }

    ~IocpBackend() override {
        PostQueuedCompletionStatus(port_.get(), 0, 0, nullptr);
        if (worker_.joinable()) worker_.join();
    }

private:
    static constexpr DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                    FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
                                    FILE_NOTIFY_CHANGE_CREATION;

    explicit IocpBackend(Callback callback)
        : callback_{std::move(callback)}, port_{CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1)} {}

    static bool reparse_point(const std::filesystem::path& path) {
        const auto attributes = GetFileAttributesW(path.c_str());
        return attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    }

    bool add_directory(const std::filesystem::path& root) {
        std::error_code error;
        const auto normalized = std::filesystem::absolute(root, error).lexically_normal();
        if (error) return false;
        if (watched_.contains(normalized) || reparse_point(normalized)) return false;
        if (!std::filesystem::is_directory(normalized, error)) return false;
        Handle directory{CreateFileW(
            normalized.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr)};
        if (!directory.valid()) return false;
        auto watch = std::make_unique<DirectoryWatch>();
        watch->directory = std::move(directory);
        watch->root = normalized;
        if (!CreateIoCompletionPort(watch->directory.get(), port_.get(),
                                    reinterpret_cast<ULONG_PTR>(watch.get()), 0)) return false;
        if (!read(*watch)) return false;
        watched_.insert(normalized);
        watches_.push_back(std::move(watch));
        return true;
    }

    static bool read(DirectoryWatch& watch) {
        watch.overlapped = {};
        return ReadDirectoryChangesW(
                   watch.directory.get(), watch.buffer.data(), static_cast<DWORD>(watch.buffer.size()), TRUE,
                   filter, nullptr, &watch.overlapped, nullptr) != FALSE;
    }

    void run() {
        while (true) {
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            OVERLAPPED* overlapped = nullptr;
            const auto ok = GetQueuedCompletionStatus(port_.get(), &bytes, &key, &overlapped, INFINITE);
            if (key == 0 && overlapped == nullptr) break;
            auto* watch = reinterpret_cast<DirectoryWatch*>(key);
            if (!watch) continue;
            if (!ok || bytes == 0) notify(callback_, watch->root, Change::rescan);
            else handle(*watch, bytes);
            if (!read(*watch)) notify(callback_, watch->root, Change::rescan);
        }
    }

    void handle(const DirectoryWatch& watch, DWORD bytes) {
        DWORD offset = 0;
        while (offset + sizeof(FILE_NOTIFY_INFORMATION) <= bytes) {
            const auto* event = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(watch.buffer.data() + offset);
            const auto name_size = event->FileNameLength / sizeof(wchar_t);
            const auto path = watch.root / std::wstring{event->FileName, name_size};
            Change action = Change::modified;
            if (event->Action == FILE_ACTION_ADDED) action = Change::added;
            else if (event->Action == FILE_ACTION_REMOVED) action = Change::removed;
            else if (event->Action == FILE_ACTION_RENAMED_OLD_NAME ||
                     event->Action == FILE_ACTION_RENAMED_NEW_NAME) action = Change::moved;
            notify(callback_, path, action);
            if (event->NextEntryOffset == 0 || offset + event->NextEntryOffset > bytes) break;
            offset += event->NextEntryOffset;
        }
    }

    Callback callback_;
    Handle port_;
    std::vector<std::unique_ptr<DirectoryWatch>> watches_;
    std::set<std::filesystem::path> watched_;
    std::jthread worker_;
};

}  // namespace

Result<std::unique_ptr<WatcherBackend>> create_native_watcher(
    std::span<const std::filesystem::path> roots,
    Callback callback) {
    return IocpBackend::create(roots, std::move(callback));
}

}  // namespace liteapp::fs

#endif