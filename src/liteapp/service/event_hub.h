#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "liteapp/service/router.h"

namespace liteapp {

// Broadcasts application events to every page subscribed through a long-lived async request
// (for example "events.subscribe"). While a recording is open, published events are kept and replayed to pages that
// subscribe later, so a page that reloads mid-operation catches up on everything the operation has shown so far.
class EventHub {
public:
    // Keeps the responder open and emits each published event to it until the client cancels or the hub closes.
    void subscribe(ServiceResponder responder);
    // Emits an event to every subscriber; `record` keeps it for replay while a recording is open.
    void publish(std::string_view type, const nlohmann::json& payload, bool record = true);
    // Starts a new recording, discarding the previous one.
    void begin_recording();
    // Stops recording and discards the recorded events.
    void end_recording();
    // Completes every subscription.
    void close();

private:
    std::mutex mutex_;
    std::vector<std::shared_ptr<ServiceResponder>> subscribers_;
    std::vector<std::pair<std::string, nlohmann::json>> recording_;
    bool recording_open_{false};
};

}  // namespace liteapp
