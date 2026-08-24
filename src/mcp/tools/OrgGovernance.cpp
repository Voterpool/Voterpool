#include "mcp/tools/ToolHelpers.h"

namespace voterpool::mcp {

ToolDef defLeaveOrganization() {
    return ToolDef{
        "leave_organization",
        "Leave an organization; the last ADMIN must transfer admin rights first",
        [] { return schemaObject({{"org_id", Json::Value("string")}}, {"org_id"}); },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            auto orgId = argUuid(args, "org_id");
            if (!orgId.ok()) return orgId.error();

            // Org-лок сериализует подсчёт админов и декремент
            // total_voting_power с конкурентными мутациями (design D5).
            auto orgLock = tc.app.orgLocks.acquire(orgId.value());
            auto orgOpt = tc.app.orgs->get(orgId.value());
            if (!orgOpt || orgOpt->status == OrgStatus::DISSOLVED)
                return RpcError::notFound("Organization", orgId.value());

            auto m = tc.app.orgs->getMembership(orgId.value(), tc.agent->agent_id);
            if (!m) {
                RpcError e = RpcError::forbidden("Agent is not a member of this organization");
                e.data["org_id"] = orgId.value();
                return e;
            }
            if (m->role == MemberRole::ADMIN) {
                int admins = 0;
                for (const auto& mem : tc.app.orgs->listMembers(orgId.value())) {
                    if (mem.status == MemberStatus::ACTIVE && mem.role == MemberRole::ADMIN) ++admins;
                }
                if (admins <= 1) {
                    return RpcError::businessRule("Last admin cannot leave; transfer admin rights first");
                }
            }

            rocksdb::WriteBatch batch;
            double power = m->voting_power;
            bool wasActive = m->status == MemberStatus::ACTIVE;
            std::int64_t now = tc.app.clock->nowSec();
            tc.app.orgs->deleteMembership(batch, orgId.value(), tc.agent->agent_id);
            Organization updated = *orgOpt;
            if (wasActive) updated.total_voting_power -= power;
            updated.updated_at = now;
            db_putOrg(tc.app, batch, updated);
            if (!tc.app.db->commit(batch)) return RpcError::internal("Storage write failed");

            Json::Value ev;
            ev["org_id"] = orgId.value();
            ev["agent_id"] = tc.agent->agent_id;
            ev["new_total_voting_power"] = updated.total_voting_power;
            tc.app.hub->deliver(SseEvent{orgId.value(), "member_left", Codec::dump(ev)});

            Json::Value out;
            out["org_id"] = orgId.value();
            out["status"] = "LEFT";
            return out;
        }};
}

ToolDef defTransferAdmin() {
    return ToolDef{
        "transfer_admin",
        "ADMIN-only: transfer the ADMIN role to another ACTIVE member (exactly one admin at any time)",
        [] {
            return schemaObject({{"org_id", Json::Value("string")},
                                 {"target_agent_id", Json::Value("string")}},
                                {"org_id", "target_agent_id"});
        },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            auto orgId = argUuid(args, "org_id");
            if (!orgId.ok()) return orgId.error();
            auto target = argUuid(args, "target_agent_id");
            if (!target.ok()) return target.error();

            // Org-лок сериализует смену ролей: инвариант «ровно один админ»
            // не должен нарушаться конкурентными transfer/leave (design D5).
            auto orgLock = tc.app.orgLocks.acquire(orgId.value());

            auto orgOpt = tc.app.orgs->get(orgId.value());
            if (!orgOpt || orgOpt->status == OrgStatus::DISSOLVED)
                return RpcError::notFound("Organization", orgId.value());
            auto requester = requireActiveMember(tc.app, orgId.value(), tc.agent->agent_id);
            if (!requester.ok()) return requester.error();
            if (requester.value().role != MemberRole::ADMIN) {
                return RpcError::forbidden("Admin privileges required to execute this action");
            }
            auto targetM = tc.app.orgs->getMembership(orgId.value(), target.value());
            if (!targetM) return RpcError::notFound("Membership", target.value());
            if (targetM->status != MemberStatus::ACTIVE) {
                return RpcError::forbidden("Target agent membership is not ACTIVE");
            }

            std::int64_t now = tc.app.clock->nowSec();
            requester.value().role = MemberRole::MEMBER;
            requester.value().updated_at = now;
            targetM->role = MemberRole::ADMIN;
            targetM->updated_at = now;

            rocksdb::WriteBatch batch;
            tc.app.orgs->putMembership(batch, requester.value());
            tc.app.orgs->putMembership(batch, *targetM);

            AuditEvent ae;
            ae.action = "ADMIN_TRANSFERRED";
            ae.org_id = orgId.value();
            ae.agent_id = target.value();
            ae.by_agent = tc.agent->agent_id;
            ae.created_at = tc.app.clock->nowMilli();
            tc.app.audit->append(batch, orgId.value(), ae);
            if (!tc.app.db->commit(batch)) return RpcError::internal("Storage write failed");

            Json::Value ev;
            ev["org_id"] = orgId.value();
            ev["previous_admin_id"] = tc.agent->agent_id;
            ev["new_admin_id"] = target.value();
            tc.app.hub->deliver(SseEvent{orgId.value(), "admin_transferred", Codec::dump(ev)});

            Json::Value out;
            out["org_id"] = orgId.value();
            out["previous_admin_id"] = tc.agent->agent_id;
            out["new_admin_id"] = target.value();
            return out;
        }};
}

}  // namespace voterpool::mcp
