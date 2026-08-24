#include "mcp/tools/ToolHelpers.h"

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
        "Create a proposal (STANDARD, or ACTION with kind APPROVE_MEMBER | UPDATE_ORG_INFO); optionally carry config_delta",
        [] {
            Json::Value configDelta = schemaObject(
                {{"consensus_model", Json::Value("string")},
                 {"quorum_percentage", Json::Value("integer")},
                 {"voting_duration_sec", Json::Value("integer")},
                 {"power_distribution", Json::Value("string")}},
                {});
            return schemaObject(
                {{"org_id", Json::Value("string")},
                 {"title", Json::Value("string")},
                 {"description", Json::Value("string")},
                 {"config_delta", std::move(configDelta)},
                 {"action", Json::Value("object")}},
                {"org_id", "title"});
        },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            auto orgId = argUuid(args, "org_id");
            if (!orgId.ok()) return orgId.error();
            auto title = argString(args, "title");
            if (!title.ok()) return title.error();

            // Org-лок: статус организации и согласованный снимок T/H читаются
            // под локом, вставка атомарна относительно роспуска и мутаций
            // состава (design D5). Быстрые проверки до лока — только fast-path.
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
                auto delta = parseOrgConfig(args["config_delta"], true);
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
                    act.target_agent_id = payload["target_agent_id"].asString();
                } else if (kindStr.value() == "UPDATE_ORG_INFO") {
                    act.kind = ActionKind::UPDATE_ORG_INFO;
                    const Json::Value& payload = actJson.get("payload", Json::Value(Json::objectValue));
                    if (!payload.isObject() || payload.empty())
                        return RpcError::invalidParams("action.payload must be a non-empty object");
                    static const char* kAllowed[] = {"name", "short_description", "description",
                                                     "category", "tags", "max_agents", "joins_per_day_limit"};
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
            tc.app.hub->deliver(SseEvent{p.org_id, "proposal_created", Codec::dump(ev)});

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
