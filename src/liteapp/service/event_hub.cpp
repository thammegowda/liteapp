#include "liteapp/service/event_hub.h"

#include <algorithm>
#include <utility>

namespace liteapp {

void EventHub::subscribe(ServiceResponder responder) {
    auto shared = std::make_shared<ServiceResponder>(std::move(responder));
    const std::lock_guard lock(mutex_);
    for (const auto& [type, payload] : recording_) static_cast<void>(shared->emit(type, payload));
    subscribers_.push_back(std::move(shared));
}

void EventHub::publish(std::string_view type, const nlohmann::json& payload, bool record) {
    const std::lock_guard lock(mutex_);
    if (recording_open_ && record) recording_.emplace_back(std::string(type), payload);
    std::erase_if(subscribers_, [&](const std::shared_ptr<ServiceResponder>& subscriber) {
        return subscriber->cancelled() || !subscriber->emit(type, payload);
    });
}

void EventHub::begin_recording() {
    const std::lock_guard lock(mutex_);
    recording_.clear();
    recording_open_ = true;
}

void EventHub::end_recording() {
    const std::lock_guard lock(mutex_);
    recording_.clear();
    recording_open_ = false;
}

void EventHub::close() {
    const std::lock_guard lock(mutex_);
    for (auto& subscriber : subscribers_) static_cast<void>(subscriber->complete());
    subscribers_.clear();
}

}  // namespace liteapp
