#pragma once

// Event plane (docs/16 §3.1).
// Единственная точка подписки и доставки SSE-событий. Реализация:
// scaling::LocalEventBus (обёртка SseHub, standalone).

#include "domain/Vote.h"
#include "server/SseHub.h"

#include <cstddef>
#include <string>
#include <vector>

namespace voterpool::scaling {

class IEventBus {
public:
    virtual ~IEventBus() = default;

    // Подписка агента на события его ACTIVE-организаций (all-orgs стрим).
    virtual void subscribeAllOrgs(const std::vector<std::string>& orgIds,
                                  const std::string& agentId,
                                  drogon::ResponseStreamPtr stream) = 0;

    // Keepalive-поток без подписок (GET /mcp): heartbeat и lifecycle общие.
    virtual void registerKeepAlive(drogon::ResponseStreamPtr stream) = 0;

    virtual void deliver(const SseEvent& event) = 0;
    virtual void heartbeat() = 0;
    virtual void shutdownAll() = 0;
    virtual std::size_t connectionCount() = 0;
};

}  // namespace voterpool::scaling
