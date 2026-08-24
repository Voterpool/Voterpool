#include "storage/repositories/AgentRepository.h"

#include "storage/Keys.h"

namespace voterpool {

bool AgentRepository::create(const std::string& agentId, const std::string& name,
                             const std::string& apiKeyHash, std::string* outErr) {
    if (get(agentId)) {
        if (outErr) *outErr = "agent already exists";
        return false;
    }
    Agent a;
    a.agent_id = agentId;
    a.name = name;
    a.api_key_hash = apiKeyHash;
    a.created_at = clock_.nowSec();
    a.updated_at = a.created_at;

    rocksdb::WriteBatch batch;
    db_.put(batch, "default", Keys::agent(agentId), Codec::serializeAgent(a));
    db_.put(batch, "cf_auth", Keys::auth(apiKeyHash), agentId);
    if (!db_.commit(batch)) {
        if (outErr) *outErr = "storage write failed";
        return false;
    }
    MetricsRegistry::instance().setGauge(
        "voterpool_agents_total", {}, static_cast<std::int64_t>(count()));
    return true;
}

std::optional<Agent> AgentRepository::get(const std::string& agentId) {
    auto v = db_.get("default", Keys::agent(agentId));
    if (!v) return std::nullopt;
    return Codec::deserializeAgent(*v);
}

bool AgentRepository::put(const Agent& agent) {
    rocksdb::WriteBatch batch;
    db_.put(batch, "default", Keys::agent(agent.agent_id), Codec::serializeAgent(agent));
    return db_.commit(batch);
}

std::int64_t AgentRepository::count() {
    std::int64_t n = 0;
    auto it = db_.newIterator("default");
    for (it->Seek("agent:"); it->Valid() && it->key().ToString().rfind("agent:", 0) == 0; it->Next()) ++n;
    return n;
}

std::string AgentRepository::resolveAgentByTokenHash(const std::string& hash) {
    auto v = db_.get("cf_auth", Keys::auth(hash));
    return v.value_or("");
}

}  // namespace voterpool
