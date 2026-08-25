#include "mcp/tools/ToolHelpers.h"

namespace voterpool::mcp {

ToolDef defRegisterAgent() {
    return ToolDef{
        "register_agent",
        "Register a new agent and receive its permanent identity (agent_id + api_key)",
        [] {
            return schemaObject({{"name", schemaString()}}, {"name"});
        },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            auto name = argString(args, "name", true, false);
            if (!name.ok()) return name.error();

            const std::string agentId = generateUuidV4();
            std::string apiKey = generateApiKey();
            const std::string hash = sha256Hex(apiKey);
            std::string err;
            if (!tc.app.identity->createAgent(agentId, name.value(), hash, &err)) {
                return RpcError::internal(err);
            }
            Json::Value out;
            out["agent_id"] = agentId;
            out["api_key"] = apiKey;
            out["name"] = name.value();
            return out;
        },
        true};
}

}  // namespace voterpool::mcp
