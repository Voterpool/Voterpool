#include "tests/common/SseClient.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <sstream>

namespace voterpool::testing {

SseClient::SseClient(const std::string& host, int port, const std::string& token, int timeoutMs) {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) return;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        close(fd_);
        fd_ = -1;
        return;
    }
    if (connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd_);
        fd_ = -1;
        return;
    }
    std::string req = "GET /mcp/events HTTP/1.1\r\n";
    req += "Host: " + host + ":" + std::to_string(port) + "\r\n";
    req += "Accept: text/event-stream\r\n";
    req += "Authorization: Bearer " + token + "\r\n";
    req += "\r\n";
    if (send(fd_, req.data(), req.size(), 0) < 0) {
        close(fd_);
        fd_ = -1;
    }
}

SseClient::~SseClient() {
    if (fd_ >= 0) close(fd_);
}

bool SseClient::fillBuffer(int timeoutMs) {
    pollfd p{fd_, POLLIN, 0};
    int ready = poll(&p, 1, timeoutMs);
    if (ready <= 0) return false;
    char buf[4096];
    ssize_t n = recv(fd_, buf, sizeof(buf), 0);
    if (n <= 0) return false;
    buffer_.append(buf, static_cast<size_t>(n));
    return true;
}

std::optional<SseClient::Event> SseClient::nextEvent(int timeoutMs) {
    size_t deadline = 0;
    while (deadline < static_cast<size_t>(timeoutMs)) {
        size_t end = buffer_.find("\n\n");
        if (end != std::string::npos) {
            std::string frame = buffer_.substr(0, end);
            buffer_.erase(0, end + 2);
            Event ev;
            std::istringstream stream(frame);
            std::string line;
            while (std::getline(stream, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.rfind("event: ", 0) == 0) ev.event = line.substr(7);
                else if (line.rfind("data: ", 0) == 0) {
                    if (!ev.data.empty()) ev.data += "\n";
                    ev.data += line.substr(6);
                } else if (line == ": keep-alive") {
                    ev.event = "__keepalive__";
                    return ev;
                }
            }
            if (!ev.event.empty()) return ev;
            continue;
        }
        auto t0 = std::chrono::steady_clock::now();
        if (!fillBuffer(timeoutMs)) return std::nullopt;
        deadline += static_cast<size_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    }
    return std::nullopt;
}

bool SseClient::sawKeepAliveWithin(int timeoutMs) {
    for (;;) {
        auto ev = nextEvent(timeoutMs);
        if (!ev.has_value()) return false;
        if (ev->event == "__keepalive__") return true;
    }
}

}  // namespace voterpool::testing
