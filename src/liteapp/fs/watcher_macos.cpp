#include "liteapp/fs/watcher_backend.h"

#if defined(__APPLE__)

#include <limits>
#include <string>
#include <vector>

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>

namespace liteapp::fs {
namespace {

std::string utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::filesystem::path path_from_cf(CFStringRef value) {
    const auto size = CFStringGetMaximumSizeForEncoding(CFStringGetLength(value), kCFStringEncodingUTF8) + 1;
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!CFStringGetCString(value, text.data(), size, kCFStringEncodingUTF8)) return {};
    text.resize(std::char_traits<char>::length(text.c_str()));
    return std::filesystem::path{std::u8string{
        reinterpret_cast<const char8_t*>(text.data()), text.size()}};
}

class FseventsBackend final : public WatcherBackend {
public:
    static Result<std::unique_ptr<WatcherBackend>> create(
        std::span<const std::filesystem::path> roots,
        Callback callback) {
        std::vector<std::filesystem::path> existing;
        for (const auto& root : roots) if (std::filesystem::is_directory(root)) existing.push_back(root);
        if (existing.empty()) return std::unexpected(Error{"watcher", "no watch roots exist"});
        auto backend = std::unique_ptr<FseventsBackend>{new FseventsBackend{std::move(existing),
                                                                           std::move(callback)}};
        if (!backend->start()) {
            return std::unexpected(Error{"watcher", "failed to create FSEvents stream"});
        }
        return backend;
    }

    ~FseventsBackend() override {
        FSEventStreamStop(stream_);
        FSEventStreamInvalidate(stream_);
        dispatch_sync_f(queue_, nullptr, [](void*) {});
        FSEventStreamRelease(stream_);
#if !OS_OBJECT_USE_OBJC
        dispatch_release(queue_);
#endif
    }

private:
    FseventsBackend(std::vector<std::filesystem::path> roots, Callback callback)
        : roots_{std::move(roots)}, callback_{std::move(callback)} {}

    static void event(
        ConstFSEventStreamRef,
        void* context,
        std::size_t count,
        void* paths,
        const FSEventStreamEventFlags flags[],
        const FSEventStreamEventId[]) {
        auto& self = *static_cast<FseventsBackend*>(context);
        const auto values = static_cast<CFArrayRef>(paths);
        for (std::size_t index = 0; index < count; ++index) {
            const auto path = path_from_cf(
                static_cast<CFStringRef>(CFArrayGetValueAtIndex(values, static_cast<CFIndex>(index))));
            const auto flag = flags[index];
            constexpr auto dropped = kFSEventStreamEventFlagMustScanSubDirs |
                                     kFSEventStreamEventFlagUserDropped |
                                     kFSEventStreamEventFlagKernelDropped |
                                     kFSEventStreamEventFlagEventIdsWrapped;
            if ((flag & dropped) != 0) { notify(self.callback_, path, Change::rescan); continue; }
            Change action = Change::modified;
            if ((flag & kFSEventStreamEventFlagItemCreated) != 0) action = Change::added;
            else if ((flag & kFSEventStreamEventFlagItemRemoved) != 0) action = Change::removed;
            else if ((flag & kFSEventStreamEventFlagItemRenamed) != 0) action = Change::moved;
            notify(self.callback_, path, action);
        }
    }

    bool start() {
        std::vector<CFStringRef> strings;
        for (const auto& root : roots_) {
            const auto text = utf8(root);
            if (text.size() > static_cast<std::size_t>(std::numeric_limits<CFIndex>::max())) continue;
            if (auto value = CFStringCreateWithBytes(kCFAllocatorDefault,
                    reinterpret_cast<const UInt8*>(text.data()), static_cast<CFIndex>(text.size()),
                    kCFStringEncodingUTF8, false)) {
                strings.push_back(value);
            }
        }
        std::vector<const void*> values{strings.begin(), strings.end()};
        if (values.size() > static_cast<std::size_t>(std::numeric_limits<CFIndex>::max())) values.clear();
        const auto paths = CFArrayCreate(kCFAllocatorDefault, values.data(),
            static_cast<CFIndex>(values.size()), &kCFTypeArrayCallBacks);
        for (const auto value : strings) CFRelease(value);
        FSEventStreamContext context{0, this, nullptr, nullptr, nullptr};
        const auto flags = kFSEventStreamCreateFlagUseCFTypes | kFSEventStreamCreateFlagFileEvents |
                           kFSEventStreamCreateFlagNoDefer;
        stream_ = paths ? FSEventStreamCreate(kCFAllocatorDefault, event, &context, paths,
            kFSEventStreamEventIdSinceNow, 0.1, flags) : nullptr;
        if (paths) CFRelease(paths);
        if (!stream_) return false;
        queue_ = dispatch_queue_create("liteapp.fs.watcher", DISPATCH_QUEUE_SERIAL);
        if (!queue_) {
            FSEventStreamInvalidate(stream_);
            FSEventStreamRelease(stream_);
            stream_ = nullptr;
            return false;
        }
        FSEventStreamSetDispatchQueue(stream_, queue_);
        if (FSEventStreamStart(stream_)) return true;
        FSEventStreamInvalidate(stream_);
        FSEventStreamRelease(stream_);
        stream_ = nullptr;
#if !OS_OBJECT_USE_OBJC
        dispatch_release(queue_);
#endif
        queue_ = nullptr;
        return false;
    }

    std::vector<std::filesystem::path> roots_;
    Callback callback_;
    FSEventStreamRef stream_{};
    dispatch_queue_t queue_{};
};

}  // namespace

Result<std::unique_ptr<WatcherBackend>> create_native_watcher(
    std::span<const std::filesystem::path> roots,
    Callback callback) {
    return FseventsBackend::create(roots, std::move(callback));
}

}  // namespace liteapp::fs

#endif