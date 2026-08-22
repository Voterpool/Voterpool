#include "mcp/tools/ToolHelpers.h"

namespace voterpool::mcp {

ToolDef defGetProposals() {
    return ToolDef{
        "get_proposals",
        "List proposals of an organization with aggregated vote counters (filter: ACTIVE | COMPLETED | ALL)",
        [] {
            return schemaObject({{"org_id", Json::Value("string")}, {"filter", Json::Value("string")}},
                                {"org_id"});
        },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            auto orgId = argUuid(args, "org_id");
            if (!orgId.ok()) return orgId.error();
            std::string filter = "ALL";
            if (args.isMember("filter")) {
                if (!args["filter"].isString()) return RpcError::invalidParams("filter must be a string");
                filter = args["filter"].asString();
                if (filter != "ACTIVE" && filter != "COMPLETED" && filter != "ALL")
                    return RpcError::invalidParams("filter must be ACTIVE, COMPLETED or ALL");
            }

            auto orgOpt = tc.app.orgs->get(orgId.value());
            if (!orgOpt) return RpcError::notFound("Organization", orgId.value());
            auto member = requireActiveMember(tc.app, orgId.value(), tc.agent->agent_id);
            if (!member.ok()) return member.error();

            Json::Value out(Json::arrayValue);
            for (const auto& p : tc.app.proposals->listByOrg(orgId.value())) {
                const bool active = p.status == ProposalStatus::ACTIVE;
                if (filter == "ACTIVE" && !active) continue;
                if (filter == "COMPLETED" && active) continue;
                Json::Value item;
                item["proposal_id"] = p.proposal_id;
                item["title"] = p.title;
                item["status"] = toString(p.status);
                item["created_at"] = static_cast<Json::Int64>(p.created_at);
                item["expires_at"] = static_cast<Json::Int64>(p.expires_at);
                item["yes_power"] = p.yes_power;
                item["no_power"] = p.no_power;
                item["abstain_power"] = p.abstain_power;
                item["voters_count"] = static_cast<Json::Int64>(p.voters_count);
                item["total_voting_power_at_creation"] = p.total_voting_power_at_creation;
                out.append(item);
            }
            return out;
        }};
}

}  // namespace voterpool::mcp
