#include "server/SseHub.h"

#include "core/Metrics.h"

#include <drogon/HttpResponse.h>

#include <spdlog/spdlog.h>

namespace voterpool {

void SseHub::registerStreams(const std::vector<std::string>& orgIds, const std::string& agentId,
                             drogon::ResponseStreamPtr stream) {
    auto holder = std::make_shared<drogon::ResponseStreamPtr>(std::move(stream));
    std::lock_guard lock(mutex_);
    uint64_t id = nextId_++;
    allConnections_.emplace_back(id, Conn{holder, "events", std::chrono::steady_clock::now()});
    for (const auto& org : orgIds) {
        Subscriber s;
        s.holder = holder;
        s.agentId = agentId;
        s.id = id;
        byOrg_[org].push_back(std::move(s));
    }
    MetricsRegistry::instance().setGauge("voterpool_sse_connections", {},
                                         static_cast<std::int64_t>(connectionCount()));
}

void SseHub::registerKeepAlive(drogon::ResponseStreamPtr stream) {
    auto holder = std::make_shared<drogon::ResponseStreamPtr>(std::move(stream));
    std::lock_guard lock(mutex_);
    allConnections_.emplace_back(nextId_++, Conn{std::move(holder), "keepalive", std::chrono::steady_clock::now()});
    MetricsRegistry::instance().setGauge("voterpool_sse_connections", {},
                                         static_cast<std::int64_t>(connectionCount()));
}

bool SseHub::sendRaw(const StreamHolder& holder, const std::string& raw) {
    if (!holder || !*holder) return false;
    bool ok = (*holder)->send(raw);
    if (!ok) spdlog::warn("SSE send failed (len {})", raw.size());
    return ok;
}

bool SseHub::sendFrame(const StreamHolder& holder, const std::string& eventType, const std::string& payloadJson) {
    std::string frame = "event: " + eventType + "\ndata: " + payloadJson + "\n\n";
    return sendRaw(holder, frame);
}

void SseHub::pruneLocked() {
    std::vector<StreamHolder> dead;
    allConnections_.erase(
        std::remove_if(allConnections_.begin(), allConnections_.end(),
                       [](const std::pair<uint64_t, Conn>& c) {
                           if (c.second.holder && *c.second.holder) return false;
                           // Диагностика: кто и когда закрыл соединение.
                           const auto ageSec = std::chrono::duration_cast<std::chrono::seconds>(
                                                   std::chrono::steady_clock::now() - c.second.opened)
                                                   .count();
                           spdlog::info("SSE {} stream closed by peer after {}s", c.second.kind, ageSec);
                           return true;
                       }),
        allConnections_.end());
    for (auto it = byOrg_.begin(); it != byOrg_.end();) {
        auto& subs = it->second;
        subs.erase(std::remove_if(subs.begin(), subs.end(),
                                  [&dead](Subscriber& s) {
                                      if (s.holder && *s.holder) return false;
                                      dead.push_back(s.holder);
                                      return true;
                                  }),
                   subs.end());
        if (subs.empty()) it = byOrg_.erase(it);
        else ++it;
    }
    if (!dead.empty()) {
        MetricsRegistry::instance().setGauge("voterpool_sse_connections", {},
                                             static_cast<std::int64_t>(connectionCount()));
    }
}

void SseHub::deliver(const SseEvent& event) {
    {
        std::lock_guard<std::mutex> lk(recordMutex_);
        if (recordedForTests_.size() < 4096) recordedForTests_.push_back(event);
    }
    std::vector<StreamHolder> targets;
    {
        std::lock_guard lock(mutex_);
        pruneLocked();
        auto it = byOrg_.find(event.org_id);
        if (it == byOrg_.end()) return;
        for (auto& s : it->second) targets.push_back(s.holder);
    }
    size_t failures = 0;
    for (const auto& h : targets) {
        if (!sendFrame(h, event.event_type, event.payload_json)) ++failures;
        else MetricsRegistry::instance().incCounter("voterpool_sse_events_sent_total", {{"event_type", event.event_type}});
    }
    if (failures > 0) {
        MetricsRegistry::instance().incCounter("voterpool_sse_write_failures_total", {{"event_type", event.event_type}},
                                               static_cast<std::int64_t>(failures));
        std::lock_guard lock(mutex_);
        pruneLocked();
    }
}

std::vector<SseEvent> SseHub::eventsForTests() const {
    std::lock_guard<std::mutex> lk(recordMutex_);
    return recordedForTests_;
}

void SseHub::heartbeat() {
    std::vector<Conn> all;
    {
        std::lock_guard lock(mutex_);
        pruneLocked();
        for (auto& [id, c] : allConnections_) all.push_back(c);
    }
    size_t failures = 0;
    for (const auto& c : all) {
        if (!sendRaw(c.holder, ": keep-alive\n\n")) {
            ++failures;
            const auto ageSec = std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::steady_clock::now() - c.opened)
                                    .count();
            spdlog::warn("SSE heartbeat write failed (kind {}, age {}s)", c.kind, ageSec);
        }
    }
    if (failures > 0) {
        MetricsRegistry::instance().incCounter("voterpool_sse_write_failures_total", {{"event_type", "keep-alive"}},
                                               static_cast<std::int64_t>(failures));
    }
}

void SseHub::shutdownAll() {
    std::lock_guard lock(mutex_);
    for (auto& [id, c] : allConnections_) {
        if (c.holder && *c.holder) {
            (*c.holder)->send("event: server_shutdown\ndata: {}\n\n");
            (*c.holder)->close();
        }
    }
    byOrg_.clear();
    MetricsRegistry::instance().setGauge("voterpool_sse_connections", {}, 0);
}

size_t SseHub::connectionCount() { return allConnections_.size(); }

}  // namespace voterpool
