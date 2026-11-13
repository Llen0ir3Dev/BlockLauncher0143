#include <nexus/bridge/agent.hpp>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <nexus/core/fs.hpp>
#include <nexus/core/log.hpp>
#include <nexus/core/time.hpp>
#include <nexus/core/version.hpp>

#include "socket_util.hpp"

namespace nexus::bridge {

std::string default_socket_path() {
    const std::string directory = fs::join(fs::nexus_home(), "run");
    if (!fs::ensure_directory(directory)) {
        return fs::join("/tmp", "nexus-runtime-" + std::to_string(::getuid()) + ".sock");
    }
    return fs::join(directory, "nexus-runtime.sock");
}

struct BridgeAgent::Connection {
    int fd = -1;
    std::string address;
    std::mutex write_mutex;
    std::atomic<bool> open{true};

    ~Connection() {
        if (fd >= 0) ::close(fd);
    }
};

BridgeAgent::BridgeAgent(BridgeAgentOptions options) : options_(std::move(options)) {}

BridgeAgent::~BridgeAgent() {
    stop();
}

void BridgeAgent::set_handler(BridgeRequestHandler handler) {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    handler_ = std::move(handler);
}

void BridgeAgent::register_method(const std::string& method, BridgeRequestHandler handler) {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    methods_[method] = std::move(handler);
}

bool BridgeAgent::start(std::string& error) {
    error.clear();
    if (running_.load()) {
        error = "bridge agent is already running";
        return false;
    }
    if (options_.socket_path.empty()) {
        error = "bridge socket path is empty";
        return false;
    }
    if (options_.socket_path.size() >= sizeof(sockaddr_un::sun_path)) {
        error = "bridge socket path is too long for AF_UNIX";
        return false;
    }
    if (!fs::ensure_parent(options_.socket_path)) {
        error = "cannot create bridge socket directory for " + options_.socket_path;
        return false;
    }
    struct stat info {};
    if (::lstat(options_.socket_path.c_str(), &info) == 0) {
        if (!S_ISSOCK(info.st_mode)) {
            error = "refusing to remove non-socket path " + options_.socket_path;
            return false;
        }
        if (::unlink(options_.socket_path.c_str()) != 0) {
            error = "cannot unlink stale bridge socket: " + std::string(std::strerror(errno));
            return false;
        }
    }
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        error = std::string("socket() failed: ") + std::strerror(errno);
        return false;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, options_.socket_path.c_str(), sizeof(address.sun_path) - 1);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        error = std::string("bind() failed for ") + options_.socket_path + ": " +
                std::strerror(errno);
        ::close(fd);
        return false;
    }
    ::chmod(options_.socket_path.c_str(), 0600);
    if (::listen(fd, options_.backlog) != 0) {
        error = std::string("listen() failed: ") + std::strerror(errno);
        ::close(fd);
        ::unlink(options_.socket_path.c_str());
        return false;
    }
    listen_fd_ = fd;
    messages_received_ = 0;
    messages_sent_ = 0;
    errors_sent_ = 0;
    started_at_ = iso8601_now();
    running_.store(true);
    accept_thread_ = std::thread([this] { accept_loop(); });
    NEXUS_LOG_INFO("bridge agent listening on " + options_.socket_path);
    return true;
}

void BridgeAgent::stop() {
    if (!running_.exchange(false)) {
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        return;
    }
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (accept_thread_.joinable()) accept_thread_.join();

    std::vector<std::shared_ptr<Connection>> connections;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        connections = connections_;
    }
    for (const auto& connection : connections) {
        connection->open.store(false);
        if (connection->fd >= 0) ::shutdown(connection->fd, SHUT_RDWR);
    }
    for (auto& thread : connection_threads_) {
        if (thread.joinable()) thread.join();
    }
    connection_threads_.clear();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        connections_.clear();
    }
    if (!options_.socket_path.empty()) ::unlink(options_.socket_path.c_str());
    NEXUS_LOG_INFO("bridge agent stopped");
}

void BridgeAgent::accept_loop() {
    while (running_.load()) {
        sockaddr_un address{};
        socklen_t length = sizeof(address);
        const int fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&address), &length);
        if (fd < 0) {
            if (errno == EINTR) continue;
            if (!running_.load()) break;
            NEXUS_LOG_WARN(std::string("bridge accept failed: ") + std::strerror(errno));
            continue;
        }
        auto connection = std::make_shared<Connection>();
        connection->fd = fd;
        connection->address = "unix";
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (connections_.size() >= options_.max_clients) {
                const std::string line = encode_message(make_error_response(
                    "", error_code::kUnavailable, "bridge agent is at its client limit"));
                std::string ignored;
                detail::send_all(fd, line, ignored);
                connection->open.store(false);
                continue;
            }
            connections_.push_back(connection);
        }
        NEXUS_LOG_DEBUG("bridge client connected");
        connection_threads_.emplace_back([this, connection] { connection_loop(connection); });
    }
}

void BridgeAgent::connection_loop(std::shared_ptr<Connection> connection) {
    std::string buffer;
    char chunk[8192];
    while (running_.load() && connection->open.load()) {
        const ssize_t received = ::recv(connection->fd, chunk, sizeof(chunk), 0);
        if (received < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (received == 0) break;
        buffer.append(chunk, static_cast<size_t>(received));
        if (buffer.size() > options_.max_message_bytes) {
            std::string ignored;
            detail::send_all(connection->fd,
                             encode_message(make_error_response(
                                 "", error_code::kParse, "message exceeds the maximum size")),
                             ignored);
            break;
        }
        size_t newline = 0;
        while ((newline = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, newline);
            buffer.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ++messages_received_;
            }
            BridgeMessage message;
            BridgeError decode_error;
            if (!decode_message(line, message, decode_error)) {
                send_message(connection, make_error_response("", decode_error.code,
                                                             decode_error.message));
                continue;
            }
            if (message.kind != MessageKind::Request) {
                send_message(connection, make_error_response(
                                             message.id, error_code::kInvalidRequest,
                                             "only request messages are accepted by the agent"));
                continue;
            }
            BridgeRequestContext context;
            context.client_address = connection->address;
            context.request_id = message.id;
            nlohmann::json result = nlohmann::json::object();
            BridgeError error;
            const bool ok = dispatch(message.method, message.params, result, error, context);
            if (ok) {
                send_message(connection, make_response(message.id, std::move(result)));
            } else {
                send_message(connection, make_error_response(message.id, error.code,
                                                             error.message, error.data));
            }
        }
    }
    connection->open.store(false);
    if (connection->fd >= 0) ::shutdown(connection->fd, SHUT_RDWR);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        connections_.erase(std::remove(connections_.begin(), connections_.end(), connection),
                           connections_.end());
    }
    NEXUS_LOG_DEBUG("bridge client disconnected");
}

bool BridgeAgent::dispatch(const std::string& method, const nlohmann::json& params,
                           nlohmann::json& result, BridgeError& error,
                           const BridgeRequestContext& context) {
    try {
        if (method == "ping") {
            result = {{"protocol", kProtocolName}, {"version", nexus::kVersion},
                      {"agent", "nexus-bridge"}, {"pid", static_cast<int64_t>(::getpid())},
                      {"startedAt", started_at_}};
            return true;
        }
        if (method == "status") {
            result = stats();
            return true;
        }
        BridgeRequestHandler handler;
        {
            std::lock_guard<std::mutex> lock(handler_mutex_);
            const auto method_it = methods_.find(method);
            if (method_it != methods_.end()) handler = method_it->second;
            else handler = handler_;
        }
        if (!handler) {
            error.code = error_code::kMethodNotFound;
            error.message = "unknown bridge method: " + method;
            return false;
        }
        return handler(method, params, result, error, context);
    } catch (const std::exception& ex) {
        error.code = error_code::kInternal;
        error.message = std::string("bridge handler threw: ") + ex.what();
        return false;
    } catch (...) {
        error.code = error_code::kInternal;
        error.message = "bridge handler threw an unknown exception";
        return false;
    }
}

bool BridgeAgent::send_message(const std::shared_ptr<Connection>& connection,
                               const BridgeMessage& message) {
    if (!connection || connection->fd < 0) return false;
    std::lock_guard<std::mutex> lock(connection->write_mutex);
    std::string error;
    const bool ok = detail::send_all(connection->fd, encode_message(message), error);
    {
        std::lock_guard<std::mutex> stats_lock(mutex_);
        if (ok) ++messages_sent_;
        else ++errors_sent_;
    }
    if (!ok) {
        connection->open.store(false);
        if (connection->fd >= 0) ::shutdown(connection->fd, SHUT_RDWR);
    }
    return ok;
}

size_t BridgeAgent::publish(const std::string& event, nlohmann::json data) {
    std::vector<std::shared_ptr<Connection>> connections;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        connections = connections_;
    }
    const BridgeMessage message = make_event(event, std::move(data));
    size_t delivered = 0;
    for (const auto& connection : connections) {
        if (!connection->open.load()) continue;
        if (send_message(connection, message)) ++delivered;
    }
    return delivered;
}

size_t BridgeAgent::client_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.size();
}

nlohmann::json BridgeAgent::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {
        {"protocol", kProtocolName},
        {"version", nexus::kVersion},
        {"socket", options_.socket_path},
        {"running", running_.load()},
        {"clients", connections_.size()},
        {"maxClients", options_.max_clients},
        {"messagesReceived", messages_received_},
        {"messagesSent", messages_sent_},
        {"sendErrors", errors_sent_},
        {"startedAt", started_at_}
    };
}

} // namespace nexus::bridge
