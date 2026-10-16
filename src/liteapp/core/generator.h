#pragma once

#include <concepts>
#include <coroutine>
#include <exception>
#include <iterator>
#include <optional>
#include <utility>

namespace liteapp {

template<std::movable T>
class Generator {
public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;

    struct promise_type {
        Generator get_return_object() { return Generator{Handle::from_promise(*this)}; }
        static std::suspend_always initial_suspend() noexcept { return {}; }
        static std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(T value) {
            current.emplace(std::move(value));
            return {};
        }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { exception = std::current_exception(); }

        std::optional<T> current;
        std::exception_ptr exception;
    };

    class Iterator {
    public:
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::input_iterator_tag;

        Iterator& operator++() {
            resume(handle_);
            return *this;
        }
        void operator++(int) { ++*this; }
        const T& operator*() const { return *handle_.promise().current; }
        const T* operator->() const { return &**this; }
        bool operator==(std::default_sentinel_t) const noexcept { return !handle_ || handle_.done(); }

    private:
        friend class Generator;
        explicit Iterator(Handle handle) : handle_{handle} {}
        Handle handle_{};
    };

    Generator() = default;
    ~Generator() { if (handle_) handle_.destroy(); }
    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;
    Generator(Generator&& other) noexcept
        : handle_{std::exchange(other.handle_, {})}, started_{std::exchange(other.started_, false)} {}
    Generator& operator=(Generator&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
            started_ = std::exchange(other.started_, false);
        }
        return *this;
    }

    Iterator begin() {
        if (handle_ && !started_) {
            started_ = true;
            resume(handle_);
        }
        return Iterator{handle_};
    }
    static std::default_sentinel_t end() noexcept { return {}; }

private:
    explicit Generator(Handle handle) : handle_{handle} {}
    static void resume(Handle handle) {
        if (!handle || handle.done()) return;
        handle.resume();
        if (handle.done() && handle.promise().exception) std::rethrow_exception(handle.promise().exception);
    }

    Handle handle_{};
    bool started_{false};
};

}  // namespace liteapp