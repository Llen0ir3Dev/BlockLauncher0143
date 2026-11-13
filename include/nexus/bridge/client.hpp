#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexus/bridge/protocol.hpp>

namespace nexus::bridge {

struct BridgeClientOptions {
    std::string socket_path;
    int timeout_ms = 5000;
    size_t max_message_bytes = kMaxMessageBytes;
};

// Synchronous client used by the launcher, the CLI and integration tests.
// It is safe to use from one thread only.  Events received while waiting for a
// response are queued and can be drained with poll_event().
class BridgeClient {
public:
    BridgeClient() = default;
    explicit BridgeClient(BridgeClientOptions options) : options_(std::move(options)) {}
    ~BridgeClient();
    BridgeClient(const BridgeClient&) = delete;
    BridgeClient& operator=(const BridgeClient&) = delete;

    bool connect(std::string& error);
    void disconnect();
    bool connected() const { return fd_ >= 0; }
    const std::string& socket_path() const { return options_.socket_path; }

    // Sends a request and blocks until the matching response arrives.
    bool call(const std::string& method, const nlohmann::json& params,
              nlohmann::json& result, BridgeError& error);

    bool ping(std::string* agent_version, std::string& error);
    bool status(nlohmann::json& result, BridgeError& error);

    // Pops a queued event, waiting up to timeout_ms when the queue is empty.
    bool poll_event(BridgeMessage& event, int timeout_ms, std::string& error);

private:
    bool send_line(const std::string& line, std::string& error);
    bool read_line(std::string& line, int timeout_ms, std::string& error);
    void queue_event(BridgeMessage message);

    BridgeClientOptions options_;
    int fd_ = -1;
    std::string read_buffer_;
    uint64_t next_id_ = 1;
    std::mutex queue_mutex_;
    std::deque<BridgeMessage> events_;
};

} // namespace nexus::bridge
