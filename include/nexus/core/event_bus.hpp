#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace nexus {

struct Event {
    std::string name;
    nlohmann::json data = nlohmann::json::object();
    bool cancelled = false;
    std::string source;
};

using EventHandler = std::function<void(Event&)>;
using SubscriptionId = uint64_t;

struct Subscription {
    SubscriptionId id = 0;
    std::string event;
    int priority = 0;
    EventHandler handler;
};

class EventBus {
public:
    SubscriptionId subscribe(const std::string& event, EventHandler handler, int priority = 0);
    bool unsubscribe(SubscriptionId id);
    void unsubscribe_all(const std::string& owner_prefix = "");
    bool emit(Event& event);
    bool emit(const std::string& name, nlohmann::json data = nlohmann::json::object(),
              std::string source = "");
    std::vector<std::string> event_names() const;
    size_t handler_count() const;

private:
    mutable std::mutex mutex_;
    std::vector<Subscription> subscriptions_;
    SubscriptionId next_id_ = 1;
};

} // namespace nexus
