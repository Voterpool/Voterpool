#include "mcp/tools/ToolHelpers.h"

namespace voterpool::mcp {
namespace {

auto steadyStart() { return std::chrono::steady_clock::now(); }

}  // namespace

ToolDef defCastVote() {
    return ToolDef{
        "cast_vote",
        "Cast a vote (YES | NO | ABSTAIN) on an active proposal; decision must be allowed by the org consensus model",
        [] {
            Json::Value decision = schemaEnumOf({"YES", "NO", "ABSTAIN"},
                "Case-sensitive UPPER_CASE. Which decisions are accepted depends on the org "
                "consensus model: MAJORITY and QUORUM_PERCENTAGE accept YES|NO only; CONSENT "
                "accepts YES|NO|ABSTAIN. Wrong decision -> -32005 with data.allowed");
            return schemaObject({{"proposal_id", schemaString(
                "UUID of an ACTIVE proposal you may vote on (from get_proposals)")},
                                 {"decision", std::move(decision)}},
                                {"proposal_id", "decision"});
        },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            auto t0 = steadyStart();
            auto proposalId = argUuid(args, "proposal_id");
            if (!proposalId.ok()) return proposalId.error();
            auto decisionStr = argString(args, "decision");
            if (!decisionStr.ok()) return decisionStr.error();

            VoteDecision decision;
            const std::string& d = decisionStr.value();
            if (d == "YES") decision = VoteDecision::YES;
            else if (d == "NO") decision = VoteDecision::NO;
            else if (d == "ABSTAIN") decision = VoteDecision::ABSTAIN;
            else return RpcError::invalidParams("decision must be YES, NO or ABSTAIN");

            auto receipt = tc.app.engine->castVote(tc.agent->agent_id, proposalId.value(), decision);
            MetricsRegistry::instance().observe("voterpool_cast_vote_duration_seconds",
                                                std::chrono::duration<double>(steadyStart() - t0).count());
            if (!receipt.ok()) return receipt.error();

            Json::Value out;
            out["proposal_id"] = receipt.value().proposal_id;
            out["decision"] = receipt.value().decision;
            out["power_applied"] = receipt.value().power_applied;
            out["proposal_status"] = toString(receipt.value().proposal_status);
            out["current_yes_power"] = receipt.value().current_yes_power;
            out["current_no_power"] = receipt.value().current_no_power;
            out["message"] = receipt.value().proposal_status == ProposalStatus::ACTIVE
                                 ? "Vote recorded."
                                 : std::string("Consensus reached. Proposal ") +
                                       toString(receipt.value().proposal_status) + ".";
            return out;
        }};
}

ToolDef defListMembers() {
    return ToolDef{
        "list_members",
        "List ACTIVE members of an organization with roles and voting power",
        [] { return schemaObject({{"org_id", schemaString(
            "UUID of the organization whose ACTIVE members to list")}},
            {"org_id"}); },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            auto orgId = argUuid(args, "org_id");
            if (!orgId.ok()) return orgId.error();
            auto orgOpt = tc.app.orgs->get(orgId.value());
            if (!orgOpt) return RpcError::notFound("Organization", orgId.value());
            auto member = requireActiveMember(tc.app, orgId.value(), tc.agent->agent_id);
            if (!member.ok()) return member.error();

            Json::Value out(Json::arrayValue);
            for (const auto& m : tc.app.orgs->listMembers(orgId.value())) {
                if (m.status != MemberStatus::ACTIVE) continue;
                Json::Value item;
                item["agent_id"] = m.agent_id;
                item["role"] = toString(m.role);
                item["voting_power"] = m.voting_power;
                item["status"] = toString(m.status);
                out.append(item);
            }
            return out;
        }};
}

}  // namespace voterpool::mcp
