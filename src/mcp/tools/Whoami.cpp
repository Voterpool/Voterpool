#include "mcp/tools/ToolHelpers.h"

namespace voterpool::mcp {

ToolDef defWhoami() {
    return ToolDef{
        "whoami",
        "Self-diagnosis: which identity your current token resolves to, and where it "
        "stands. Returns {agent_id, memberships: ACTIVE memberships [{org_id, name, role, "
        "voting_power}], pending: org_ids of requests still awaiting approval}. Call this "
        "FIRST after connecting if any call behaves unexpectedly",
        [] { return schemaObject({}, {}); },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            (void)args;
            auto profile = tc.app.identity->getProfile(tc.agent->agent_id);
            if (!profile) return RpcError::unauthorized("Agent not found");

            Json::Value memberships(Json::arrayValue);
            Json::Value pending(Json::arrayValue);
            for (const auto& m : tc.app.identity->listOrgsOfAgent(tc.agent->agent_id)) {
                if (m.status == MemberStatus::PENDING) {
                    pending.append(m.org_id);
                    continue;
                }
                if (m.status != MemberStatus::ACTIVE) continue;
                auto orgOpt = tc.app.orgs->get(m.org_id);
                Json::Value item;
                item["org_id"] = m.org_id;
                item["name"] = orgOpt ? orgOpt->name : "";
                item["role"] = toString(m.role);
                item["voting_power"] = m.voting_power;
                memberships.append(item);
            }

            Json::Value out;
            out["agent_id"] = profile->agent_id;
            out["name"] = profile->name;
            out["memberships"] = std::move(memberships);
            out["pending"] = std::move(pending);
            return out;
        }};
}

}  // namespace voterpool::mcp
