#pragma once

#include <mutex>
#include <set>
#include <string>
#include <string_view>

namespace liteapp {

class ActivityGate {
public:
    [[nodiscard]] bool acquire(std::string_view kind) {
        std::scoped_lock lock{mutex_};
        if (active_.contains(kind)) return false;
        active_.emplace(kind);
        return true;
    }

    void release(std::string_view kind) {
        std::scoped_lock lock{mutex_};
        active_.erase(std::string{kind});
    }

    [[nodiscard]] bool any() const {
        std::scoped_lock lock{mutex_};
        return !active_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::set<std::string, std::less<>> active_;
};

}  // namespace liteapp
