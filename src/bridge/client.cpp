#include <nexus/bridge/client.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "socket_util.hpp"

namespace nexus::bridge {

BridgeClient::~BridgeClient() {
    disconnect();
}

bool BridgeClient::connect(std::string& error) {
    disconnect();
    if (options_.socket_path.empty()) {
        error = "bridge socket path is empty";
        return false;
    }
    if (options_.socket_path.size() >= sizeof(sockaddr_un::sun_path)) {
        error = "bridge socket path is too long for AF_UNIX";
        return false;
    }
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        error = std::string("socket() failed: ") + std::strerror(errno);
        return false;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, options_.socket_path.c_str(), sizeof(address.sun_path) - 1);

    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int result = ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (result != 0 && errno == EINPROGRESS) {
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(fd, &writable);
        timeval timeout{};
        timeout.tv_sec = options_.timeout_ms / 1000;
        timeout.tv_usec = (options_.timeout_ms % 1000) * 1000;
        result = ::select(fd + 1, nullptr, &writable, nullptr,
                          options_.timeout_ms > 0 ? &timeout : nullptr);
        if (result <= 0) {
            error = result == 0 ? "connect timed out" : std::string("connect failed: ") +
                                                               std::strerror(errno);
            ::close(fd);
            return false;
        }
        int socket_error = 0;
        socklen_t length = sizeof(socket_error);
        ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &length);
        if (socket_error != 0) {
            error = std::string("connect failed: ") + std::strerror(socket_error);
            ::close(fd);
            return false;
        }
    } else if (result != 0) {
        error = std::string("connect failed: ") + std::strerror(errno);
        ::close(fd);
        return false;
    }
    ::fcntl(fd, F_SETFL, flags);
    detail::set_socket_timeout(fd, options_.timeout_ms);
    fd_ = fd;
    read_buffer_.clear();
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        events_.clear();
    }
    return true;
}

void BridgeClient::disconnect() {
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
    }
    read_buffer_.clear();
}

bool BridgeClient::send_line(const std::string& line, std::string& error) {
    if (fd_ < 0) {
        error = "not connected to a bridge agent";
        return false;
    }
    if (!detail::send_all(fd_, line, error)) {
        disconnect();
        return false;
    }
    return true;
}

bool BridgeClient::read_line(std::string& line, int timeout_ms, std::string& error) {
    if (fd_ < 0) {
        error = "not connected to a bridge agent";
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(std::max(0, timeout_ms));
    char buffer[8192];
    while (true) {
        const auto newline = read_buffer_.find('\n');
        if (newline != std::string::npos) {
            line = read_buffer_.substr(0, newline);
            read_buffer_.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
        if (read_buffer_.size() > options_.max_message_bytes) {
            error = "bridge message exceeds the maximum size";
            disconnect();
            return false;
        }
        if (timeout_ms > 0) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) {
                error = "timed out waiting for a bridge response";
                return false;
            }
            detail::set_socket_timeout(fd_, static_cast<int>(remaining));
        }
        const ssize_t received = ::recv(fd_, buffer, sizeof(buffer), 0);
        if (received < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                error = "timed out waiting for a bridge response";
                return false;
            }
            error = std::string("recv failed: ") + std::strerror(errno);
            disconnect();
            return false;
        }
        if (received == 0) {
            error = "bridge agent closed the connection";
            disconnect();
            return false;
        }
        read_buffer_.append(buffer, static_cast<size_t>(received));
    }
}

void BridgeClient::queue_event(BridgeMessage message) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    events_.push_back(std::move(message));
}

bool BridgeClient::call(const std::string& method, const nlohmann::json& params,
                        nlohmann::json& result, BridgeError& error) {
    result = nlohmann::json::object();
    error = BridgeError{};
    if (fd_ < 0) {
        error.code = error_code::kUnavailable;
        error.message = "not connected to a bridge agent";
        return false;
    }
    const std::string id = std::to_string(next_id_++);
    BridgeMessage request = make_request(id, method, params);
    std::string send_error;
    if (!send_line(encode_message(request), send_error)) {
        error.code = error_code::kUnavailable;
        error.message = send_error;
        return false;
    }
    while (true) {
        std::string line;
        std::string read_error;
        if (!read_line(line, options_.timeout_ms, read_error)) {
            error.code = read_error.find("timed out") != std::string::npos ? error_code::kTimeout
                                                                          : error_code::kUnavailable;
            error.message = read_error;
            return false;
        }
        BridgeMessage message;
        BridgeError decode_error;
        if (!decode_message(line, message, decode_error)) {
            error = decode_error;
            return false;
        }
        if (message.kind == MessageKind::Event) {
            queue_event(std::move(message));
            continue;
        }
        if (message.id != id) {
            // A stale response cannot satisfy this request; keep waiting.
            continue;
        }
        if (message.kind == MessageKind::Error) {
            error = message.error;
            return false;
        }
        if (message.kind == MessageKind::Response) {
            result = std::move(message.result);
            return true;
        }
    }
}

bool BridgeClient::ping(std::string* agent_version, std::string& error) {
    nlohmann::json result;
    BridgeError bridge_error;
    if (!call("ping", nlohmann::json::object(), result, bridge_error)) {
        error = bridge_error.message;
        return false;
    }
    if (agent_version != nullptr) {
        *agent_version = result.value("version", "");
    }
    return true;
}

bool BridgeClient::status(nlohmann::json& result, BridgeError& error) {
    return call("status", nlohmann::json::object(), result, error);
}

bool BridgeClient::poll_event(BridgeMessage& event, int timeout_ms, std::string& error) {
    error.clear();
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (!events_.empty()) {
            event = std::move(events_.front());
            events_.pop_front();
            return true;
        }
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(std::max(0, timeout_ms));
    while (true) {
        const int remaining = timeout_ms > 0
                                  ? static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        deadline - std::chrono::steady_clock::now()).count())
                                  : 0;
        if (timeout_ms > 0 && remaining <= 0) {
            error = "timed out waiting for a bridge event";
            return false;
        }
        std::string line;
        if (!read_line(line, remaining, error)) return false;
        BridgeMessage message;
        BridgeError decode_error;
        if (!decode_message(line, message, decode_error)) {
            error = decode_error.message;
            return false;
        }
        if (message.kind == MessageKind::Event) {
            event = std::move(message);
            return true;
        }
        // Ignore responses that arrive while waiting for events.
    }
}

} // namespace nexus::bridge
