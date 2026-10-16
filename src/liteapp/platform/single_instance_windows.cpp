#include "liteapp/platform/single_instance.h"

#if defined(_WIN32)

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <windows.h>

namespace liteapp::platform {
namespace {

class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_{value} {}
    ~Handle() { if (value_) CloseHandle(value_); }
    Handle(Handle&& other) noexcept : value_{std::exchange(other.value_, nullptr)} {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (value_) CloseHandle(value_);
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    [[nodiscard]] HANDLE get() const noexcept { return value_; }

private:
    HANDLE value_;
};

Error instance_error(std::string message) {
    return {"single_instance", std::move(message)};
}

bool valid_id(std::string_view id) {
    return !id.empty() && std::ranges::all_of(id, [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == '-' || character == '_';
    });
}

std::wstring object_name(std::string_view id, std::wstring_view suffix) {
    std::wstring result{L"Local\\liteapp-"};
    result.append(id.begin(), id.end());
    result.append(suffix);
    return result;
}

}  // namespace

struct SingleInstance::Impl {
    Handle mutex;
    Handle activation;
    Handle stop;
    std::mutex handler_mutex;
    std::function<void()> handler;
    bool pending{false};
    std::jthread worker;

    ~Impl() { if (stop.get()) SetEvent(stop.get()); }

    void start() {
        worker = std::jthread{[this] {
            const HANDLE events[]{activation.get(), stop.get()};
            while (true) {
                const auto result = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                if (result != WAIT_OBJECT_0) return;
                std::function<void()> callback;
                {
                    std::scoped_lock lock{handler_mutex};
                    callback = handler;
                    pending = !callback;
                }
                if (callback) callback();
            }
        }};
    }
};

SingleInstance::SingleInstance(std::unique_ptr<Impl> impl) : impl_{std::move(impl)} {}

Result<std::optional<SingleInstance>> SingleInstance::acquire(std::string_view app_id) {
    if (!valid_id(app_id)) return std::unexpected(instance_error("app id is not a safe instance name"));
    const auto mutex_name = object_name(app_id, L"-mutex");
    const auto event_name = object_name(app_id, L"-activate");
    const auto raw_mutex = CreateMutexW(nullptr, TRUE, mutex_name.c_str());
    if (!raw_mutex) return std::unexpected(instance_error("Windows instance mutex could not be created"));
    const auto already_exists = GetLastError() == ERROR_ALREADY_EXISTS;
    Handle mutex{raw_mutex};
    if (already_exists) {
        const auto wait = WaitForSingleObject(mutex.get(), 5000);
        if (wait == WAIT_OBJECT_0) {
            ReleaseMutex(mutex.get());
            const Handle activation{OpenEventW(EVENT_MODIFY_STATE, FALSE, event_name.c_str())};
            if (!activation.get() || !SetEvent(activation.get())) {
                return std::unexpected(instance_error("existing app instance could not be activated"));
            }
            return std::optional<SingleInstance>{};
        }
        if (wait != WAIT_ABANDONED) {
            return std::unexpected(instance_error("existing app instance did not become ready"));
        }
    }

    auto implementation = std::make_unique<Impl>();
    implementation->mutex = std::move(mutex);
    implementation->activation = Handle{CreateEventW(nullptr, FALSE, FALSE, event_name.c_str())};
    implementation->stop = Handle{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!implementation->activation.get() || !implementation->stop.get()) {
        return std::unexpected(instance_error("Windows instance events could not be created"));
    }
    ReleaseMutex(implementation->mutex.get());
    implementation->start();
    return std::optional<SingleInstance>{SingleInstance{std::move(implementation)}};
}

SingleInstance::~SingleInstance() = default;
SingleInstance::SingleInstance(SingleInstance&&) noexcept = default;
SingleInstance& SingleInstance::operator=(SingleInstance&&) noexcept = default;

void SingleInstance::set_activation_handler(std::function<void()> handler) {
    if (!impl_) return;
    std::function<void()> pending_handler;
    {
        std::scoped_lock lock{impl_->handler_mutex};
        impl_->handler = std::move(handler);
        if (impl_->pending) pending_handler = impl_->handler;
        impl_->pending = false;
    }
    if (pending_handler) pending_handler();
}

void SingleInstance::clear_activation_handler() {
    if (!impl_) return;
    std::scoped_lock lock{impl_->handler_mutex};
    impl_->handler = {};
}

}  // namespace liteapp::platform

#endif
