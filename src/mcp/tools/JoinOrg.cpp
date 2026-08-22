#include "mcp/tools/ToolHelpers.h"

namespace voterpool::mcp {

ToolDef defJoinOrganization() {
    return ToolDef{
        "join_organization",
        "Join an organization: instant ACTIVE membership for OPEN orgs, PENDING request for CLOSED ones",
        [] { return schemaObject({{"org_id", Json::Value("string")}}, {"org_id"}); },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            auto orgId = argUuid(args, "org_id");
            if (!orgId.ok()) return orgId.error();

            auto orgOpt = tc.app.orgs->get(orgId.value());
            if (!orgOpt) return RpcError::notFound("Organization", orgId.value());
            if (orgOpt->status == OrgStatus::DISSOLVED) return RpcError::notFound("Organization", orgId.value());

            auto existing = tc.app.orgs->getMembership(orgId.value(), tc.agent->agent_id);
            if (existing && existing->status == MemberStatus::ACTIVE) {
                Json::Value out;
                out["org_id"] = orgId.value();
                out["status"] = "ACTIVE";
                out["role"] = toString(existing->role);
                out["voting_power"] = existing->voting_power;
                return out;
            }
            if (existing && existing->status == MemberStatus::PENDING) {
                Json::Value out;
                out["org_id"] = orgId.value();
                out["status"] = "PENDING";
                out["message"] = "Join request submitted. Waiting for admin approval.";
                return out;
            }

            double power =
                orgOpt->config.power_distribution == PowerDistribution::EQUAL ? 1.0 : 0.0;

            if (orgOpt->type == OrgType::OPEN) {
                if (orgOpt->max_agents > 0 &&
                    tc.app.orgs->countActiveMembers(orgId.value()) >= orgOpt->max_agents) {
                    RpcError e = RpcError::businessRule("Organization is full");
                    e.data["max_agents"] = static_cast<Json::Int64>(orgOpt->max_agents);
                    return e;
                }
                if (orgOpt->joins_per_day_limit > 0) {
                    std::int64_t used = tc.app.indexes->getJoinLimit(orgId.value(), tc.app.clock->nowSec());
                    if (used >= orgOpt->joins_per_day_limit) {
                        RpcError e = RpcError::businessRule("Daily join limit reached");
                        e.data["joins_per_day_limit"] = static_cast<Json::Int64>(orgOpt->joins_per_day_limit);
                        return e;
                    }
                }
                Membership m;
                m.org_id = orgId.value();
                m.agent_id = tc.agent->agent_id;
                m.role = MemberRole::MEMBER;
                m.voting_power = power;
                m.status = MemberStatus::ACTIVE;
                std::int64_t now = tc.app.clock->nowSec();
                m.created_at = now;
                m.updated_at = now;

                rocksdb::WriteBatch batch;
                tc.app.orgs->putMembership(batch, m);
                tc.app.indexes->incrementJoinLimit(batch, orgId.value(), now);
                Organization updated = *orgOpt;
                updated.total_voting_power += power;
                updated.updated_at = now;
                db_putOrg(tc.app, batch, updated);
                if (!tc.app.db->commit(batch)) return RpcError::internal("Storage write failed");

                Json::Value ev;
                ev["org_id"] = orgId.value();
                ev["agent_id"] = tc.agent->agent_id;
                ev["role"] = "MEMBER";
                ev["voting_power"] = power;
                ev["new_total_voting_power"] = updated.total_voting_power;
                tc.app.hub->deliver(SseEvent{orgId.value(), "member_joined", Codec::dump(ev)});

                Json::Value out;
                out["org_id"] = orgId.value();
                out["status"] = "ACTIVE";
                out["role"] = "MEMBER";
                out["voting_power"] = power;
                return out;
            }

            Membership m;
            m.org_id = orgId.value();
            m.agent_id = tc.agent->agent_id;
            m.role = MemberRole::MEMBER;
            m.voting_power = power;
            m.status = MemberStatus::PENDING;
            std::int64_t now = tc.app.clock->nowSec();
            m.created_at = now;
            m.updated_at = now;

            rocksdb::WriteBatch batch;
            tc.app.orgs->putMembership(batch, m);
            tc.app.indexes->addPending(batch, orgId.value(), tc.agent->agent_id, std::to_string(now));
            if (!tc.app.db->commit(batch)) return RpcError::internal("Storage write failed");

            Json::Value out;
            out["org_id"] = orgId.value();
            out["status"] = "PENDING";
            out["message"] = "Join request submitted. Waiting for admin approval.";
            return out;
        }};
}

}  // namespace voterpool::mcp
