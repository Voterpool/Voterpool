#pragma once

#include <optional>
#include <string>

namespace voterpool::testing {

class SseClient {
public:
    struct Event {
        std::string event;
        std::string data;
    };

    SseClient(const std::string& host, int port, const std::string& token, int timeoutMs = 5000);
    ~SseClient();

    SseClient(const SseClient&) = delete;
    SseClient& operator=(const SseClient&) = delete;

    bool connected() const { return fd_ >= 0; }

    std::optional<Event> nextEvent(int timeoutMs = 5000);
    bool sawKeepAliveWithin(int timeoutMs);

private:
    bool fillBuffer(int timeoutMs);

    int fd_ = -1;
    std::string buffer_;
};

}  // namespace voterpool::testing
