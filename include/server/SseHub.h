#pragma once

#include "domain/Vote.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>

namespace drogon {
class HttpResponse;
using HttpResponsePtr = std::shared_ptr<HttpResponse>;
class ResponseStream;
using ResponseStreamPtr = std::unique_ptr<ResponseStream>;
}  // namespace drogon

namespace voterpool {

class SseHub {
public:
    using StreamHolder = std::shared_ptr<drogon::ResponseStreamPtr>;

    void registerStreams(const std::vector<std::string>& orgIds, const std::string& agentId,
                         drogon::ResponseStreamPtr stream);

    void deliver(const SseEvent& event);
    // Тестовый доступ к истории доставки (прецедент dispatchToolForTests).
    std::vector<SseEvent> eventsForTests() const;
    void heartbeat();
    void shutdownAll();
    size_t connectionCount();

private:
    struct Subscriber {
        StreamHolder holder;
        std::string agentId;
        uint64_t id;
    };

    static bool sendFrame(const StreamHolder& holder, const std::string& eventType, const std::string& payloadJson);
    static bool sendRaw(const StreamHolder& holder, const std::string& raw);

    void pruneLocked();

    std::mutex mutex_;
    mutable std::mutex recordMutex_;
    std::vector<SseEvent> recordedForTests_;
    std::unordered_map<std::string, std::vector<Subscriber>> byOrg_;
    std::vector<std::pair<uint64_t, StreamHolder>> allConnections_;
    uint64_t nextId_ = 1;
};

}  // namespace voterpool
