#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexus/bridge/protocol.hpp>

namespace nexus::bridge {

struct BridgeAgentOptions {
    std::string socket_path;
    size_t max_message_bytes = kMaxMessageBytes;
    int backlog = 8;
    size_t max_clients = 8;
};

struct BridgeRequestContext {
    std::string client_address;
    std::string request_id;
};

// Request handler.  Return false to send an error response; exceptions are
// converted into an internal error response.
using BridgeRequestHandler =
    std::function<bool(const std::string& method, const nlohmann::json& params,
                       nlohmann::json& result, BridgeError& error,
                       const BridgeRequestContext& context)>;

// Unix-socket server implementing the NEXUS bridge protocol.  The agent is a
// transport only: the runtime or game-side helper owns the actual methods.
class BridgeAgent {
public:
    explicit BridgeAgent(BridgeAgentOptions options);
    ~BridgeAgent();
    BridgeAgent(const BridgeAgent&) = delete;
    BridgeAgent& operator=(const BridgeAgent&) = delete;

    void set_handler(BridgeRequestHandler handler);
    void register_method(const std::string& method, BridgeRequestHandler handler);

    bool start(std::string& error);
    void stop();
    bool running() const { return running_.load(); }
    const std::string& socket_path() const { return options_.socket_path; }

    // Sends an event notification to every connected client.
    size_t publish(const std::string& event, nlohmann::json data = nlohmann::json::object());

    size_t client_count() const;
    nlohmann::json stats() const;

    struct Connection;

private:
    void accept_loop();
    void connection_loop(std::shared_ptr<Connection> connection);
    bool dispatch(const std::string& method, const nlohmann::json& params,
                  nlohmann::json& result, BridgeError& error,
                  const BridgeRequestContext& context);
    bool send_message(const std::shared_ptr<Connection>& connection,
                      const BridgeMessage& message);

    BridgeAgentOptions options_;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<Connection>> connections_;
    std::vector<std::thread> connection_threads_;
    BridgeRequestHandler handler_;
    std::map<std::string, BridgeRequestHandler> methods_;
    mutable std::mutex handler_mutex_;
    uint64_t messages_received_ = 0;
    uint64_t messages_sent_ = 0;
    uint64_t errors_sent_ = 0;
    std::string started_at_;
};

// Default runtime socket path: $NEXUS_HOME/run/nexus-runtime.sock, falling
// back to /tmp/nexus-runtime-<uid>.sock.
std::string default_socket_path();

} // namespace nexus::bridge
