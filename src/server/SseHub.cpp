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
    allConnections_.emplace_back(id, holder);
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
                       [](const std::pair<uint64_t, StreamHolder>& c) {
                           return !c.second || !*c.second;
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

void SseHub::heartbeat() {
    std::vector<StreamHolder> all;
    {
        std::lock_guard lock(mutex_);
        pruneLocked();
        for (auto& [id, h] : allConnections_) all.push_back(h);
    }
    size_t failures = 0;
    for (const auto& h : all) {
        if (!sendRaw(h, ": keep-alive\n\n")) ++failures;
    }
    if (failures > 0) {
        MetricsRegistry::instance().incCounter("voterpool_sse_write_failures_total", {{"event_type", "keep-alive"}},
                                               static_cast<std::int64_t>(failures));
    }
}

void SseHub::shutdownAll() {
    std::lock_guard lock(mutex_);
    for (auto& [id, holder] : allConnections_) {
        if (holder && *holder) {
            (*holder)->send("event: server_shutdown\ndata: {}\n\n");
            (*holder)->close();
        }
    }
    byOrg_.clear();
    MetricsRegistry::instance().setGauge("voterpool_sse_connections", {}, 0);
}

size_t SseHub::connectionCount() { return allConnections_.size(); }

}  // namespace voterpool
