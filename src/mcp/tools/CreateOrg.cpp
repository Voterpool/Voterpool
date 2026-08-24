#include "mcp/tools/ToolHelpers.h"

#include <set>

namespace voterpool::mcp {

ToolDef defCreateOrganization() {
    return ToolDef{
        "create_organization",
        "Create an organization with consensus settings; the creator becomes its single ADMIN",
        [] {
            Json::Value config = schemaObject(
                {{"consensus_model", schemaString()},
                 {"quorum_percentage", schemaInteger()},
                 {"voting_duration_sec", schemaInteger()},
                 {"power_distribution", schemaString()}},
                {"consensus_model", "voting_duration_sec"});
            return schemaObject(
                {{"name", schemaString()},
                 {"short_description", schemaString()},
                 {"description", schemaString()},
                 {"tags", schemaArrayOf("string")},
                 {"type", schemaString()},
                 {"max_agents", schemaInteger()},
                 {"joins_per_day_limit", schemaInteger()},
                 {"config", std::move(config)}},
                {"name", "type", "config"});
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

            bool configRequired = args.isMember("config");
            auto cfg = parseOrgConfig(args["config"], true);
            if (!cfg.ok()) return cfg.error();

            // Дубликат имени: O(1) lookup по резидентному реестру (только ACTIVE).
            if (auto dup = tc.app.orgNames->findActiveByName(Keys::nameLower(name.value()))) {
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

            tc.app.orgNames->add(org.org_id, Keys::nameLower(org.name));
            refreshOrgGauges(tc.app);

            Json::Value out;
            out["org_id"] = org.org_id;
            out["name"] = org.name;
            out["short_description"] = org.short_description;
            out["tags"] = Codec::tagsToJson(org.tags);
            out["type"] = toString(org.type);
            out["role"] = "ADMIN";
            out["voting_power"] = creator.voting_power;
            out["config"] = orgConfigJson(org.config);
            return out;
        }};
}

}  // namespace voterpool::mcp
