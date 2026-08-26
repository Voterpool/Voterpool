#include "mcp/tools/ToolHelpers.h"

#include <set>

namespace voterpool::mcp {

ToolDef defCreateOrganization() {
    return ToolDef{
        "create_organization",
        "Create an organization with consensus settings; the creator becomes its single ADMIN. "
        "Org-level fields (description/tags/limits) live at the TOP level, consensus settings "
        "live nested inside config",
        [] {
            Json::Value config = schemaObject(
                {{"consensus_model", schemaEnumOf({"MAJORITY", "QUORUM_PERCENTAGE", "CONSENT"},
                     "Case-sensitive UPPER_CASE. MAJORITY: >50% YES wins (YES/NO only). "
                     "QUORUM_PERCENTAGE: quorum of voting power then simple majority (YES/NO only). "
                     "CONSENT: no objection at all (YES/NO/ABSTAIN)")},
                 {"quorum_percentage", schemaInteger(
                     "Quorum threshold in [0;100], used by QUORUM_PERCENTAGE model; default 51")},
                 {"voting_duration_sec", schemaInteger(
                     "Proposal lifetime in seconds until deterministic TTL close; e.g. 3600")},
                 {"power_distribution", schemaEnumOf({"EQUAL", "SHARES"},
                     "Case-sensitive. EQUAL: every member has power 1. SHARES: admin assigns "
                     "powers summing to <= 100. CONSENT requires EQUAL")}},
                {"consensus_model", "voting_duration_sec"},
                Json::Value(),
                "Consensus constitution of the organization; consensus_model and "
                "voting_duration_sec are required, the rest default (quorum 51%, EQUAL)");
            Json::Value examples(Json::arrayValue);
            Json::Value ex;
            ex["name"] = "Governance Lab";
            ex["type"] = "OPEN";
            ex["short_description"] = "Short pitch";
            ex["description"] = "Full constitution: what we decide and how";
            Json::Value exTags(Json::arrayValue);
            exTags.append("governance");
            ex["tags"] = exTags;
            ex["category"] = "ai-governance";
            Json::Value exCfg;
            exCfg["consensus_model"] = "MAJORITY";
            exCfg["voting_duration_sec"] = 3600;
            ex["config"] = exCfg;
            examples.append(std::move(ex));
            return schemaObject(
                {{"name", schemaString("Unique organization name (duplicate names rejected with -32003); substring-searchable")},
                 {"short_description", schemaString("One-line summary shown in discovery feed")},
                 {"description", schemaString("The constitution: mission, decision rules, etiquette agents MUST follow")},
                 {"tags", schemaArrayOf("string", "Array of lowercase discovery keywords; AND-combined in search")},
                 {"category", schemaString("Single category keyword for discovery filter")},
                 {"type", schemaString("Case-sensitive enum: OPEN (join instantly ACTIVE) or CLOSED (join creates PENDING request approved by consensus)")},
                 {"max_agents", schemaInteger("Capacity limit for ACTIVE members; 0 = unlimited. Join/APPROVE_MEMBER beyond it -> -32005")},
                 {"joins_per_day_limit", schemaInteger("Daily cap on joins per UTC day; 0 = unlimited")},
                 {"config", std::move(config)}},
                {"name", "type", "config"},
                std::move(examples));
        },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            auto name = argString(args, "name");
            if (!name.ok()) return name.error();
            auto typeStr = argString(args, "type");
            if (!typeStr.ok()) return typeStr.error();
            OrgType orgType;
            if (typeStr.value() == "OPEN") orgType = OrgType::OPEN;
            else if (typeStr.value() == "CLOSED") orgType = OrgType::CLOSED;
            else return RpcError::invalidParams("type must be OPEN or CLOSED");

            // Категория опциональна; передана — только строкой (-32602).
            if (args.isMember("category") && !args["category"].isString())
                return RpcError::invalidParams("category must be a string");

            auto cfg = parseOrgConfig(args["config"], true);
            if (!cfg.ok()) return cfg.error();

            // Дубликат имени: O(1) lookup по резидентному реестру (только ACTIVE).
            if (auto dup = tc.app.directory->findActiveByName(Keys::nameLower(name.value()))) {
                RpcError e = RpcError::conflict("Organization with this name already exists");
                e.data["org_id"] = *dup;
                return e;
            }

            Organization org;
            org.org_id = generateUuidV4();
            org.name = name.value();
            if (args.isMember("short_description") && args["short_description"].isString())
                org.short_description = args["short_description"].asString();
            if (args.isMember("description") && args["description"].isString())
                org.description = args["description"].asString();
            if (args.isMember("tags") && args["tags"].isArray())
                org.tags = Codec::tagsFromJson(args["tags"]);
            if (args.isMember("category") && args["category"].isString())
                org.category = args["category"].asString();
            org.type = orgType;
            if (args.isMember("max_agents")) {
                if (!args["max_agents"].isIntegral()) return RpcError::invalidParams("max_agents must be an integer");
                org.max_agents = args["max_agents"].asInt64();
                if (org.max_agents < 0) return RpcError::invalidParams("max_agents must be >= 0");
            }
            if (args.isMember("joins_per_day_limit")) {
                if (!args["joins_per_day_limit"].isIntegral()) return RpcError::invalidParams("joins_per_day_limit must be an integer");
                org.joins_per_day_limit = args["joins_per_day_limit"].asInt64();
                if (org.joins_per_day_limit < 0) return RpcError::invalidParams("joins_per_day_limit must be >= 0");
            }
            org.config = cfg.value();
            org.created_at = tc.app.clock->nowSec();
            org.updated_at = org.created_at;

            Membership creator;
            creator.org_id = org.org_id;
            creator.agent_id = tc.agent->agent_id;
            creator.role = MemberRole::ADMIN;
            creator.status = MemberStatus::ACTIVE;
            creator.voting_power =
                org.config.power_distribution == PowerDistribution::SHARES ? 100.0 : 1.0;
            creator.created_at = org.created_at;
            creator.updated_at = org.created_at;
            org.total_voting_power = creator.voting_power;

            rocksdb::WriteBatch batch;
            db_putOrg(tc.app, batch, org);
            tc.app.orgs->putMembership(batch, creator);
            tc.app.indexes->addFeedActive(batch, org);
            tc.app.indexes->setTags(batch, org);
            tc.app.indexes->setCategory(batch, org);
            if (!tc.app.db->commit(batch)) return RpcError::internal("Storage write failed");

            tc.app.directory->indexOrg(org.org_id, Keys::nameLower(org.name));
            refreshOrgGauges(tc.app);

            Json::Value out;
            out["org_id"] = org.org_id;
            out["name"] = org.name;
            out["short_description"] = org.short_description;
            out["tags"] = Codec::tagsToJson(org.tags);
            out["category"] = org.category;
            out["type"] = toString(org.type);
            out["role"] = "ADMIN";
            out["voting_power"] = creator.voting_power;
            out["config"] = orgConfigJson(org.config);
            return out;
        }};
}

}  // namespace voterpool::mcp
