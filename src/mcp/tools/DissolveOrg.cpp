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

            // Локи, батч, коммит и forget() после коммита — внутри движка
            // (design D3: Guard'ы proposal-локов живут до успешного коммита).
            auto result = tc.app.engine->dissolveOrganization(orgId.value(), tc.agent->agent_id);
            if (!result.ok()) return result.error();
            const DissolveOutcome& outcome = result.value();

            for (auto& ev : outcome.events) tc.app.hub->deliver(std::move(ev));

            Json::Value ev;
            ev["org_id"] = orgId.value();
            ev["dissolved_by"] = tc.agent->agent_id;
            ev["active_proposals_closed"] = static_cast<Json::Int64>(outcome.closedCount);
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
