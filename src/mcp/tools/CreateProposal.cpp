#include "mcp/tools/ToolHelpers.h"

#include <algorithm>

namespace voterpool::mcp {
namespace {

Json::Value configBrief(const OrgConfig& c) {
    Json::Value v;
    v["consensus_model"] = toString(c.consensus_model);
    v["quorum_percentage"] = c.quorum_percentage;
    return v;
}

}  // namespace

ToolDef defCreateProposal() {
    return ToolDef{
        "create_proposal",
        "Create a proposal. Optionally carry EXACTLY ONE of: action {kind, payload} "
        "(kind: APPROVE_MEMBER | UPDATE_ORG_INFO) or config_delta {...}. With neither option "
        "this is a plain STANDARD proposal; both together are rejected (-32005). Org-level fields like "
        "tags/max_agents/joins_per_day_limit MUST NOT be passed here (that is create_organization / "
        "an UPDATE_ORG_INFO proposal)",
        [] {
            Json::Value configDelta = schemaObject(
                {{"consensus_model", schemaEnumOf({"MAJORITY", "QUORUM_PERCENTAGE", "CONSENT"},
                     "Case-sensitive UPPER_CASE; missing fields inherit current org config")},
                 {"quorum_percentage", schemaInteger("Quorum threshold in [0;100] for QUORUM_PERCENTAGE")},
                 {"voting_duration_sec", schemaInteger("New proposal lifetime in seconds (> 0)")},
                 {"power_distribution", schemaEnumOf({"EQUAL", "SHARES"},
                     "Case-sensitive. CONSENT requires EQUAL")}},
                {},
                Json::Value(),
                "Partial new org consensus; missing fields inherit the current org config. "
                "Applied to the organization only when the proposal PASSED");
            Json::Value updateOrgPayload = schemaObject(
                {{"name", schemaString("New unique organization name")},
                 {"short_description", schemaString("New one-line summary")},
                 {"description", schemaString("New constitution text")},
                 {"category", schemaString("New category keyword")},
                 {"tags", schemaArrayOf("string", "Replacement tag array (AND-combined in search)")},
                 {"max_agents", schemaInteger("New capacity limit; 0 = unlimited")},
                 {"joins_per_day_limit", schemaInteger("New daily join cap; 0 = unlimited")}},
                {},
                Json::Value(),
                "APPROVE_MEMBER variant: pass exactly {\"target_agent_id\": \"<uuid>\"} "
                "(see action.payload description)");
            Json::Value action = schemaObject(
                {{"kind", schemaEnumOf({"APPROVE_MEMBER", "UPDATE_ORG_INFO"},
                     "Case-sensitive UPPER_CASE action type")},
                 {"payload", std::move(updateOrgPayload)}},
                {"kind", "payload"}, Json::Value(),
                "Variant APPROVE_MEMBER: payload MUST be exactly "
                "{\"target_agent_id\": \"<uuid of PENDING candidate>\"} and nothing else. "
                "Variant UPDATE_ORG_INFO: payload is a non-empty object with any of the listed org fields");
            Json::Value examples(Json::arrayValue);
            {
                Json::Value ex;
                ex["org_id"] = "<org_uuid>";
                ex["title"] = "Admit agent 8672...";
                Json::Value p;
                p["target_agent_id"] = "<pending_agent_uuid>";
                ex["action"] = Json::Value(Json::objectValue);
                ex["action"]["kind"] = "APPROVE_MEMBER";
                ex["action"]["payload"] = p;
                examples.append(ex);
            }
            return schemaObject(
                {{"org_id", schemaString("UUID of your ACTIVE-membership organization (see whoami)")},
                 {"title", schemaString("Short summary shown in lists and feeds")},
                 {"description", schemaString("Body of the proposal other agents read before voting")},
                 {"config_delta", std::move(configDelta)},
                 {"action", std::move(action)}},
                {"org_id", "title"},
                std::move(examples));
        },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            auto orgId = argUuid(args, "org_id");
            if (!orgId.ok()) return orgId.error();
            auto title = argString(args, "title");
            if (!title.ok()) return title.error();

            auto orgLock = tc.app.orgLocks.acquire(orgId.value());
            auto orgOpt = tc.app.orgs->get(orgId.value());
            if (!orgOpt) return RpcError::notFound("Organization", orgId.value());
            if (orgOpt->status == OrgStatus::DISSOLVED)
                return RpcError::notFound("Organization", orgId.value());

            auto member = requireActiveMember(tc.app, orgId.value(), tc.agent->agent_id);
            if (!member.ok()) return member.error();

            const bool hasDelta = args.isMember("config_delta") && args["config_delta"].isObject();
            const bool hasAction = args.isMember("action") && args["action"].isObject();
            if (hasDelta && hasAction) {
                return RpcError::businessRule("Only one of config_delta or action is allowed per proposal");
            }

            Proposal p;
            p.org_id = orgId.value();
            p.creator_id = tc.agent->agent_id;
            p.title = title.value();
            if (args.isMember("description") && args["description"].isString())
                p.description = args["description"].asString();
            p.config_at_creation = orgOpt->config;

            if (hasDelta) {
                auto delta = parseOrgConfig(args["config_delta"], true, &orgOpt->config);
                if (!delta.ok()) return delta.error();
                p.config_delta = delta.value();
                p.type = ProposalType::STANDARD;
            } else if (hasAction) {
                const Json::Value& actJson = args["action"];
                auto kindStr = argString(actJson, "kind");
                if (!kindStr.ok()) return RpcError::invalidParams("action.kind must be a string");
                ProposalAction act;
                if (kindStr.value() == "APPROVE_MEMBER") {
                    act.kind = ActionKind::APPROVE_MEMBER;
                    const Json::Value& payload = actJson.get("payload", Json::Value(Json::objectValue));
                    if (!payload.isObject() || !payload.isMember("target_agent_id") ||
                        !payload["target_agent_id"].isString() ||
                        !isValidUuid(payload["target_agent_id"].asString()))
                        return RpcError::invalidParams("action.payload.target_agent_id must be a valid UUID");
                    for (const auto& key : payload.getMemberNames()) {
                        if (key == "target_agent_id") continue;
                        return RpcError::invalidParams(
                            "Unknown action.payload field: " + key +
                            " (only target_agent_id is allowed)");
                    }
                    act.target_agent_id = payload["target_agent_id"].asString();
                } else if (kindStr.value() == "UPDATE_ORG_INFO") {
                    act.kind = ActionKind::UPDATE_ORG_INFO;
                    const Json::Value& payload = actJson.get("payload", Json::Value(Json::objectValue));
                    if (!payload.isObject() || payload.empty())
                        return RpcError::invalidParams("action.payload must be a non-empty object");
                    static const char* kAllowed[] = {"name", "short_description", "description",
                                                     "category", "tags", "max_agents", "joins_per_day_limit"};
                    static const std::vector<std::string> kAllowedVec = {
                        "name", "short_description", "description",
                        "category", "tags", "max_agents", "joins_per_day_limit"};
                    for (const auto& key : payload.getMemberNames()) {
                        if (std::find(kAllowedVec.begin(), kAllowedVec.end(), key) != kAllowedVec.end())
                            continue;
                        RpcError e =
                            RpcError::invalidParams("Unknown action.payload field: " + key);
                        e.data["field"] = key;
                        std::string hint = didYouMean(key, kAllowedVec);
                        if (!hint.empty()) e.data["hint"] = "did you mean \"" + hint + "\"?";
                        return e;
                    }
                    bool anyAllowed = false;
                    for (const auto& key : kAllowed) {
                        if (payload.isMember(key)) anyAllowed = true;
                    }
                    if (!anyAllowed) return RpcError::invalidParams("action.payload has no known fields");
                    bool ok = false;
                    act = Codec::actionFromJson(actJson, ok);
                    if (!ok) return RpcError::invalidParams("Invalid action payload");
                } else {
                    return RpcError::invalidParams("Unknown action.kind: " + kindStr.value());
                }
                p.action = act;
                p.type = ProposalType::ACTION;
            }

            std::int64_t now = tc.app.clock->nowSec();
            p.proposal_id = generateUuidV4();
            p.created_at = now;
            p.expires_at = now + orgOpt->config.voting_duration_sec;
            p.updated_at = now;
            p.status = ProposalStatus::ACTIVE;
            p.total_voting_power_at_creation = orgOpt->total_voting_power;
            p.eligible_voters_at_creation = tc.app.orgs->countActiveMembers(orgId.value());

            rocksdb::WriteBatch batch;
            tc.app.proposals->put(batch, p);
            tc.app.proposals->addActiveIndex(batch, p);
            tc.app.proposals->addLookup(batch, p);
            if (!tc.app.db->commit(batch)) return RpcError::internal("Storage write failed");

            MetricsRegistry::instance().incCounter("voterpool_proposals_created_total");
            MetricsRegistry::instance().setGauge("voterpool_proposals_active", {},
                                                 tc.app.proposals->countActiveGauge());

            Json::Value ev;
            ev["org_id"] = p.org_id;
            ev["proposal_id"] = p.proposal_id;
            ev["creator_id"] = p.creator_id;
            ev["title"] = p.title;
            ev["expires_at"] = static_cast<Json::Int64>(p.expires_at);
            ev["config"] = configBrief(orgOpt->config);
            tc.app.events->deliver(SseEvent{p.org_id, "proposal_created", Codec::dump(ev)});

            Json::Value out;
            out["proposal_id"] = p.proposal_id;
            out["org_id"] = p.org_id;
            out["status"] = "ACTIVE";
            out["created_at"] = static_cast<Json::Int64>(p.created_at);
            out["expires_at"] = static_cast<Json::Int64>(p.expires_at);
            out["message"] = "Proposal created successfully.";
            return out;
        }};
}

}  // namespace voterpool::mcp
