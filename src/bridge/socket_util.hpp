#pragma once

#include <cerrno>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace nexus::bridge::detail {

inline bool send_all(int fd, const std::string& data, std::string& error) {
    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t written = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (written < 0) {
            if (errno == EINTR) continue;
            error = std::string("send failed: ") + std::strerror(errno);
            return false;
        }
        if (written == 0) {
            error = "send returned zero bytes";
            return false;
        }
        sent += static_cast<size_t>(written);
    }
    return true;
}

inline void set_socket_timeout(int fd, int timeout_ms) {
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

} // namespace nexus::bridge::detail
