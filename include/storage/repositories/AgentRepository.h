#pragma once

#include "core/IClock.h"
#include "domain/Agent.h"
#include "storage/Codec.h"
#include "storage/RocksDBWrapper.h"

#include <optional>
#include <string>

namespace voterpool {

class AgentRepository {
public:
    explicit AgentRepository(RocksDBWrapper& db, IClock& clock) : db_(db), clock_(clock) {}

    bool create(const std::string& agentId, const std::string& name, const std::string& apiKeyHash,
                std::string* outErr = nullptr);
    std::optional<Agent> get(const std::string& agentId);
    bool put(const Agent& agent);
    std::int64_t count();
    std::string resolveAgentByTokenHash(const std::string& hash);

private:
    RocksDBWrapper& db_;
    IClock& clock_;
};

}  // namespace voterpool
