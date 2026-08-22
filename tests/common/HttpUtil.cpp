#include "tests/common/HttpUtil.h"

#include <json/json.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <thread>
#include <sstream>
#include <stdexcept>

namespace voterpool::testing {
namespace {

int connectHost(const std::string& host, int port, int timeoutMs) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    timeval tv{};
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    return fd;
}

std::string recvAll(int fd, size_t minBytes) {
    std::string out;
    char buf[8192];
    while (out.size() < minBytes || minBytes == 0) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        out.append(buf, static_cast<size_t>(n));
        if (minBytes == 0 && out.find("\r\n\r\n") != std::string::npos) {
            size_t headerEnd = out.find("\r\n\r\n") + 4;
            size_t contentPos = out.find("Content-Length: ");
            if (contentPos == std::string::npos) contentPos = out.find("content-length: ");
            if (contentPos != std::string::npos && contentPos < headerEnd) {
                size_t lenStart = contentPos + std::string("Content-Length: ").size();
                size_t lenEnd = out.find('\r', lenStart);
                size_t contentLength = std::stoul(out.substr(lenStart, lenEnd - lenStart));
                if (out.size() >= headerEnd + contentLength) break;
            } else {
                break;
            }
        }
    }
    return out;
}

}  // namespace

HttpResponse HttpUtil::request(const std::string& host, int port, const std::string& method,
                               const std::string& path,
                               const std::vector<std::pair<std::string, std::string>>& headers,
                               const std::string& body, int timeoutMs) {
    HttpResponse out;
    int fd = connectHost(host, port, timeoutMs);
    if (fd < 0) return out;

    std::ostringstream req;
    req << method << " " << path << " HTTP/1.1\r\n";
    req << "Host: " << host << ":" << port << "\r\n";
    req << "Connection: close\r\n";
    for (const auto& [k, v] : headers) req << k << ": " << v << "\r\n";
    if (!body.empty()) {
        req << "Content-Length: " << body.size() << "\r\n";
    }
    req << "\r\n";
    req << body;

    std::string wire = req.str();
    if (send(fd, wire.data(), wire.size(), 0) < 0) {
        close(fd);
        return out;
    }
    std::string raw = recvAll(fd, 0);
    close(fd);

    size_t statusEnd = raw.find("\r\n");
    if (statusEnd == std::string::npos) return out;
    std::istringstream statusLine(raw.substr(0, statusEnd));
    std::string httpVersion;
    statusLine >> httpVersion >> out.status;

    size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return out;
    std::istringstream headerStream(raw.substr(statusEnd + 2, headerEnd - statusEnd - 2));
    std::string line;
    while (std::getline(headerStream, line) && !line.empty()) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        if (!value.empty() && value.front() == ' ') value.erase(0, 1);
        out.headers[key] = value;
    }
    out.body = raw.substr(headerEnd + 4);
    return out;
}

HttpResponse HttpUtil::postJson(const std::string& host, int port, const std::string& path,
                                const Json::Value& body,
                                const std::vector<std::pair<std::string, std::string>>& headers,
                                int timeoutMs) {
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    return request(host, port, "POST", path, headers, Json::writeString(b, body), timeoutMs);
}

bool HttpUtil::waitForHttp(const std::string& host, int port, const std::string& path, int timeoutMs) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        HttpResponse r = get(host, port, path, {}, 500);
        if (r.status == 200) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

}  // namespace voterpool::testing
