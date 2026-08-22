#include "mcp/tools/ToolHelpers.h"

namespace voterpool::mcp {

ToolDef defDissolveOrganization() {
    return ToolDef{
        "dissolve_organization",
        "ADMIN-only: dissolve the organization (data is preserved; it disappears from search and rejects operations)",
        [] { return schemaObject({{"org_id", Json::Value("string")}}, {"org_id"}); },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            auto orgId = argUuid(args, "org_id");
            if (!orgId.ok()) return orgId.error();
            auto orgOpt = tc.app.orgs->get(orgId.value());
            if (!orgOpt) return RpcError::notFound("Organization", orgId.value());
            if (orgOpt->status == OrgStatus::DISSOLVED)
                return RpcError::notFound("Organization", orgId.value());

            auto requester = requireActiveMember(tc.app, orgId.value(), tc.agent->agent_id);
            if (!requester.ok()) return requester.error();
            if (requester.value().role != MemberRole::ADMIN) {
                return RpcError::forbidden("Admin privileges required to execute this action");
            }

            rocksdb::WriteBatch batch;
            auto closeEvents = tc.app.engine->expireAllForDissolve(batch, orgId.value());
            size_t closedCount = closeEvents.size();

            Organization updated = *orgOpt;
            updated.status = OrgStatus::DISSOLVED;
            updated.updated_at = tc.app.clock->nowSec();
            db_putOrg(tc.app, batch, updated);
            tc.app.indexes->moveFeedToDissolved(batch, updated);
            tc.app.indexes->removeName(batch, updated);
            tc.app.indexes->removeTags(batch, updated);
            tc.app.indexes->removeCategory(batch, updated);

            AuditEvent ae;
            ae.action = "ORG_DISSOLVED";
            ae.org_id = orgId.value();
            ae.agent_id = "";
            ae.by_agent = tc.agent->agent_id;
            ae.created_at = tc.app.clock->nowMilli();
            tc.app.audit->append(batch, orgId.value(), ae);
            if (!tc.app.db->commit(batch)) return RpcError::internal("Storage write failed");

            for (auto& ev : closeEvents) tc.app.hub->deliver(std::move(ev));

            Json::Value ev;
            ev["org_id"] = orgId.value();
            ev["dissolved_by"] = tc.agent->agent_id;
            ev["active_proposals_closed"] = static_cast<Json::Int64>(closedCount);
            tc.app.hub->deliver(SseEvent{orgId.value(), "organization_dissolved", Codec::dump(ev)});

            MetricsRegistry::instance().incCounter("voterpool_orgs_dissolved_total");
            refreshOrgGauges(tc.app);

            Json::Value out;
            out["org_id"] = orgId.value();
            out["status"] = "DISSOLVED";
            return out;
        }};
}

}  // namespace voterpool::mcp
