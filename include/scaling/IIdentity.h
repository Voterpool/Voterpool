#pragma once

// Identity plane (docs/16 §3.1, change add-shard-ready-ports).
// Единственная точка доступа call-site'ов к агентам: резолв токена,
// создание, профиль, список организаций агента и связь agent↔org.
// Реализации: scaling::LocalIdentity (standalone, по умолчанию).

#include "domain/Agent.h"
#include "domain/Membership.h"
#include "server/AuthProvider.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rocksdb {
class WriteBatch;
}

namespace voterpool::scaling {

class IIdentity {
public:
    virtual ~IIdentity() = default;

    // Резолв Bearer-токена → контекст агента; неизвестный токен → nullopt.
    // Формат ошибок middleware не меняется (спека shard-seams).
    virtual std::optional<AgentContext> resolveToken(const std::string& token) = 0;

    // Регистрация агента: false + текст ошибки в *outErr при отказе.
    virtual bool createAgent(const std::string& agentId, const std::string& name,
                             const std::string& apiKeyHash, std::string* outErr = nullptr) = 0;

    virtual std::optional<Agent> getProfile(const std::string& agentId) = 0;
    virtual bool putProfile(const Agent& agent) = 0;
    virtual std::int64_t countAgents() = 0;

    // Все членства агента (для get_agent и SSE all-orgs подписки).
    virtual std::vector<Membership> listOrgsOfAgent(const std::string& agentId) = 0;

    // Запись/удаление пары «membership + agent_orgs» одним WriteBatch:
    // локальная имплементация сохраняет атомарность пары той же
    // RocksDB-транзакцией, что и консенсусные записи (docs/16 §3.1 D3).
    virtual void recordMembershipLink(rocksdb::WriteBatch& batch, const Membership& m) = 0;
    virtual void removeMembershipLink(rocksdb::WriteBatch& batch, const std::string& orgId,
                                      const std::string& agentId) = 0;
};

}  // namespace voterpool::scaling
