#include "mcp/tools/ToolHelpers.h"

#include <algorithm>

namespace voterpool::mcp {

ToolDef defListPendingMembers() {
    return ToolDef{
        "list_pending_members",
        "List PENDING join requests of an organization as [{agent_id, requested_at}] sorted by "
        "request time. Available to ANY ACTIVE member - approval is consensus-based, so any member "
        "may raise an APPROVE_MEMBER proposal for a candidate",
        [] { return schemaObject({{"org_id", schemaString()}}, {"org_id"}); },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            auto orgId = argUuid(args, "org_id");
            if (!orgId.ok()) return orgId.error();

            auto org = loadOrgForOperation(tc.app, orgId.value());
            if (!org.ok()) return org.error();
            auto member = requireActiveMember(tc.app, orgId.value(), tc.agent->agent_id);
            if (!member.ok()) return member.error();

            struct Entry {
                std::string agentId;
                std::int64_t requestedAt;
            };
            std::vector<Entry> entries;
            for (const auto& candidate : tc.app.indexes->listPending(orgId.value())) {
                auto m = tc.app.orgs->getMembership(orgId.value(), candidate);
                if (!m || m->status != MemberStatus::PENDING) continue;  // рассинхрон индекса
                entries.push_back({candidate, m->created_at});
            }
            std::sort(entries.begin(), entries.end(),
                      [](const Entry& a, const Entry& b) { return a.requestedAt < b.requestedAt; });

            Json::Value out(Json::arrayValue);
            for (const auto& e : entries) {
                Json::Value item;
                item["agent_id"] = e.agentId;
                item["requested_at"] = static_cast<Json::Int64>(e.requestedAt);
                out.append(item);
            }
            return out;
        }};
}

}  // namespace voterpool::mcp
