#include "consensus/IConsensusModel.h"
#include "mcp/tools/ToolHelpers.h"

namespace voterpool::mcp {

ToolDef defUpdateVotingPower() {
    return ToolDef{
        "update_voting_power",
        "ADMIN-only: change a member's voting power (SHARES distribution only)",
        [] {
            return schemaObject({{"org_id", schemaString()},
                                 {"target_agent_id", schemaString()},
                                 {"new_power", schemaNumber()}},
                                {"org_id", "target_agent_id", "new_power"});
        },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            auto orgId = argUuid(args, "org_id");
            if (!orgId.ok()) return orgId.error();
            auto target = argUuid(args, "target_agent_id");
            if (!target.ok()) return target.error();
            auto newPower = argDouble(args, "new_power");
            if (!newPower.ok()) return newPower.error();

            // Org-лок сериализует чтение организации/членства, проверку
            // суммы ≤100% и инкрементальный пересчёт total_voting_power
            // с конкурентными мутациями состава и сил (design D5).
            auto orgLock = tc.app.orgLocks.acquire(orgId.value());

            auto orgOpt = tc.app.orgs->get(orgId.value());
            if (!orgOpt || orgOpt->status == OrgStatus::DISSOLVED)
                return RpcError::notFound("Organization", orgId.value());

            auto requester = requireActiveMember(tc.app, orgId.value(), tc.agent->agent_id);
            if (!requester.ok()) return requester.error();
            if (requester.value().role != MemberRole::ADMIN) {
                return RpcError::forbidden("Admin privileges required to execute this action");
            }
            if (orgOpt->config.power_distribution != PowerDistribution::SHARES) {
                return RpcError::businessRule("voting power is fixed in EQUAL model");
            }
            if (newPower.value() < 0.0) return RpcError::invalidParams("new_power must be >= 0");

            auto targetM = tc.app.orgs->getMembership(orgId.value(), target.value());
            if (!targetM) return RpcError::notFound("Membership", target.value());
            if (targetM->status != MemberStatus::ACTIVE) {
                return RpcError::forbidden("Target agent membership is not ACTIVE");
            }

            double othersSum =
                orgOpt->total_voting_power - targetM->voting_power;
            if (othersSum + newPower.value() > 100.0 + kEps) {
                RpcError e = RpcError::businessRule("Sum of voting powers cannot exceed 100% in SHARES model");
                e.data["attempted_sum"] = othersSum + newPower.value();
                return e;
            }

            double oldPower = targetM->voting_power;
            std::int64_t now = tc.app.clock->nowSec();
            targetM->voting_power = newPower.value();
            targetM->updated_at = now;

            Organization updated = *orgOpt;
            updated.total_voting_power = othersSum + newPower.value();
            updated.updated_at = now;

            rocksdb::WriteBatch batch;
            tc.app.orgs->putMembership(batch, *targetM);
            db_putOrg(tc.app, batch, updated);

            AuditEvent ae;
            ae.action = "POWER_CHANGED";
            ae.org_id = orgId.value();
            ae.agent_id = target.value();
            ae.by_agent = tc.agent->agent_id;
            ae.old_power = oldPower;
            ae.new_power = newPower.value();
            ae.created_at = tc.app.clock->nowMilli();
            tc.app.audit->append(batch, orgId.value(), ae);
            if (!tc.app.db->commit(batch)) return RpcError::internal("Storage write failed");

            Json::Value out;
            out["agent_id"] = target.value();
            out["new_voting_power"] = newPower.value();
            out["total_voting_power"] = updated.total_voting_power;
            return out;
        }};
}

}  // namespace voterpool::mcp
