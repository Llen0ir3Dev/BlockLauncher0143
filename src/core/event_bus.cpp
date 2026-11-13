#include <nexus/core/event_bus.hpp>

#include <algorithm>

namespace nexus {

SubscriptionId EventBus::subscribe(const std::string& event, EventHandler handler, int priority) {
    std::lock_guard<std::mutex> lock(mutex_);
    Subscription sub;
    sub.id = next_id_++;
    sub.event = event;
    sub.priority = priority;
    sub.handler = std::move(handler);
    subscriptions_.push_back(std::move(sub));
    return subscriptions_.back().id;
}

bool EventBus::unsubscribe(SubscriptionId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = std::find_if(subscriptions_.begin(), subscriptions_.end(),
                                 [id](const Subscription& s) { return s.id == id; });
    if (it == subscriptions_.end()) return false;
    subscriptions_.erase(it);
    return true;
}

void EventBus::unsubscribe_all(const std::string& owner_prefix) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (owner_prefix.empty()) {
        subscriptions_.clear();
        return;
    }
    subscriptions_.erase(std::remove_if(subscriptions_.begin(), subscriptions_.end(),
                                       [&](const Subscription& s) {
                                           return s.event.compare(0, owner_prefix.size(), owner_prefix) == 0;
                                       }),
                         subscriptions_.end());
}

bool EventBus::emit(Event& event) {
    std::vector<Subscription> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot = subscriptions_;
    }
    std::stable_sort(snapshot.begin(), snapshot.end(),
                     [](const Subscription& a, const Subscription& b) { return a.priority > b.priority; });
    for (const auto& sub : snapshot) {
        if (sub.event != event.name && sub.event != "*") continue;
        if (sub.handler) {
            try {
                sub.handler(event);
            } catch (...) {
                // A failing handler must not stop the rest of the chain.
            }
        }
        if (event.cancelled) break;
    }
    return !event.cancelled;
}

bool EventBus::emit(const std::string& name, nlohmann::json data, std::string source) {
    Event event;
    event.name = name;
    event.data = std::move(data);
    event.source = std::move(source);
    return emit(event);
}

std::vector<std::string> EventBus::event_names() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const auto& sub : subscriptions_) {
        if (std::find(names.begin(), names.end(), sub.event) == names.end()) names.push_back(sub.event);
    }
    return names;
}

size_t EventBus::handler_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscriptions_.size();
}

} // namespace nexus
