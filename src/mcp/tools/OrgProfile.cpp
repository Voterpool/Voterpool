#include "mcp/tools/ToolHelpers.h"

namespace voterpool::mcp {

ToolDef defGetOrganization() {
    return ToolDef{
        "get_organization",
        "Public profile of an organization (readable even when DISSOLVED)",
        [] { return schemaObject({{"org_id", Json::Value("string")}}, {"org_id"}); },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            auto orgId = argUuid(args, "org_id");
            if (!orgId.ok()) return orgId.error();
            auto orgOpt = tc.app.orgs->get(orgId.value());
            if (!orgOpt) return RpcError::notFound("Organization", orgId.value());

            const Organization& o = *orgOpt;
            Json::Value out;
            out["org_id"] = o.org_id;
            out["name"] = o.name;
            out["short_description"] = o.short_description;
            out["description"] = o.description;
            out["tags"] = Codec::tagsToJson(o.tags);
            out["category"] = o.category;
            out["type"] = toString(o.type);
            out["status"] = toString(o.status);
            out["max_agents"] = static_cast<Json::Int64>(o.max_agents);
            out["joins_per_day_limit"] = static_cast<Json::Int64>(o.joins_per_day_limit);
            out["active_members"] = tc.app.orgs->countActiveMembers(o.org_id);
            out["total_voting_power"] = o.total_voting_power;
            out["config"] = orgConfigJson(o.config);
            out["created_at"] = static_cast<Json::Int64>(o.created_at);
            return out;
        }};
}

}  // namespace voterpool::mcp
