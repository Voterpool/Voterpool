#include "mcp/tools/ToolHelpers.h"

namespace voterpool::mcp {

ToolDef defUpdateAgent() {
    return ToolDef{
        "update_agent",
        "Edit the caller's own profile fields: name, short_description, description, tags",
        [] {
            Json::Value tags;
            tags["type"] = "array";
            tags["items"] = schemaString();
            return schemaObject({{"name", schemaString()},
                                 {"short_description", schemaString()},
                                 {"description", schemaString()},
                                 {"tags", std::move(tags)}},
                                {});
        },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            auto agentOpt = tc.app.identity->getProfile(tc.agent->agent_id);
            if (!agentOpt) return RpcError::unauthorized("Agent not found");

            Agent& a = *agentOpt;
            if (args.isMember("name")) {
                if (!args["name"].isString()) return RpcError::invalidParams("name must be a string");
                if (!args["name"].asString().empty()) a.name = args["name"].asString();
            }
            if (args.isMember("short_description")) {
                if (!args["short_description"].isString())
                    return RpcError::invalidParams("short_description must be a string");
                a.short_description = args["short_description"].asString();
            }
            if (args.isMember("description")) {
                if (!args["description"].isString()) return RpcError::invalidParams("description must be a string");
                a.description = args["description"].asString();
            }
            if (args.isMember("tags")) {
                if (!args["tags"].isArray()) return RpcError::invalidParams("tags must be an array");
                for (const auto& t : args["tags"]) {
                    if (!t.isString()) return RpcError::invalidParams("tags items must be strings");
                }
                a.tags = Codec::tagsFromJson(args["tags"]);
            }
            a.updated_at = tc.app.clock->nowSec();
            tc.app.identity->putProfile(a);

            Json::Value profile;
            profile["agent_id"] = a.agent_id;
            profile["name"] = a.name;
            profile["short_description"] = a.short_description;
            profile["description"] = a.description;
            profile["tags"] = Codec::tagsToJson(a.tags);
            profile["created_at"] = static_cast<Json::Int64>(a.created_at);
            profile["updated_at"] = static_cast<Json::Int64>(a.updated_at);

            Json::Value out;
            out["agent_id"] = a.agent_id;
            out["profile"] = std::move(profile);
            out["updated_at"] = static_cast<Json::Int64>(a.updated_at);
            return out;
        }};
}

ToolDef defGetAgent() {
    return ToolDef{
        "get_agent",
        "Public agent profile with the list of organizations it belongs to",
        [] { return schemaObject({{"agent_id", schemaString()}}, {"agent_id"}); },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            auto agentId = argUuid(args, "agent_id");
            if (!agentId.ok()) return agentId.error();
            auto agentOpt = tc.app.identity->getProfile(agentId.value());
            if (!agentOpt) return RpcError::notFound("Agent", agentId.value());

            Json::Value orgsArr(Json::arrayValue);
            for (const auto& m : tc.app.identity->listOrgsOfAgent(agentId.value())) {
                auto orgOpt = tc.app.orgs->get(m.org_id);
                Json::Value item;
                item["org_id"] = m.org_id;
                item["name"] = orgOpt ? orgOpt->name : "";
                item["role"] = toString(m.role);
                item["status"] = toString(m.status);
                item["voting_power"] = m.voting_power;
                orgsArr.append(item);
            }

            Json::Value out;
            out["agent_id"] = agentOpt->agent_id;
            out["name"] = agentOpt->name;
            out["created_at"] = static_cast<Json::Int64>(agentOpt->created_at);
            out["organizations"] = std::move(orgsArr);
            return out;
        }};
}

}  // namespace voterpool::mcp
