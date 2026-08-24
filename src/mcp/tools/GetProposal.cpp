#include "mcp/tools/ToolHelpers.h"

namespace voterpool::mcp {

ToolDef defGetProposal() {
    return ToolDef{
        "get_proposal",
        "Fetch the full card of a single proposal by proposal_id: status, aggregated powers, "
        "timestamps, applied action/config_delta flags and (for ACTIVE members) the complete "
        "vote list with each agent's decision and power at vote",
        [] { return schemaObject({{"proposal_id", Json::Value("string")}}, {"proposal_id"}); },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            auto proposalId = argUuid(args, "proposal_id");
            if (!proposalId.ok()) return proposalId.error();

            // Изоляция: резолвим org_id одним O(1) lookup (как cast_vote), затем
            // стандартная проверка членства (docs/03 §1.3.2).
            const std::string orgId = tc.app.proposals->lookupOrg(proposalId.value());
            if (orgId.empty()) return RpcError::notFound("Proposal", proposalId.value());

            auto member = requireActiveMember(tc.app, orgId, tc.agent->agent_id);
            if (!member.ok()) return member.error();

            auto p = tc.app.proposals->get(orgId, proposalId.value());
            if (!p) return RpcError::notFound("Proposal", proposalId.value());

            Json::Value out;
            out["proposal_id"] = p->proposal_id;
            out["org_id"] = p->org_id;
            out["creator_id"] = p->creator_id;
            out["title"] = p->title;
            out["description"] = p->description;
            out["type"] = toString(p->type);
            out["status"] = toString(p->status);
            out["yes_power"] = p->yes_power;
            out["no_power"] = p->no_power;
            out["abstain_power"] = p->abstain_power;
            out["voters_count"] = static_cast<Json::Int64>(p->voters_count);
            out["total_voting_power_at_creation"] = p->total_voting_power_at_creation;
            out["eligible_voters_at_creation"] = static_cast<Json::Int64>(p->eligible_voters_at_creation);
            out["created_at"] = static_cast<Json::Int64>(p->created_at);
            out["expires_at"] = static_cast<Json::Int64>(p->expires_at);
            out["updated_at"] = static_cast<Json::Int64>(p->updated_at);
            out["config_delta_applied"] = p->config_delta_applied;
            out["action_applied"] = Json::Value(Json::nullValue);
            if (p->action_applied && p->action) {
                out["action_applied"] = toString(p->action->kind);
            }
            if (p->config_delta) out["config_delta"] = Codec::orgConfigToJson(*p->config_delta);

            Json::Value votes(Json::arrayValue);
            for (const auto& v : tc.app.votes->listByProposal(orgId, p->proposal_id)) {
                Json::Value item;
                item["agent_id"] = v.agent_id;
                item["decision"] = toString(v.decision);
                item["power_at_vote"] = v.power_at_vote;
                votes.append(item);
            }
            out["votes"] = std::move(votes);
            return out;
        }};
}

}  // namespace voterpool::mcp
