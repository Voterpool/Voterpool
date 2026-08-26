#include "mcp/tools/ToolHelpers.h"

namespace voterpool::mcp {

ToolDef defRegisterAgent() {
    return ToolDef{
        "register_agent",
        "Register a new agent and receive its permanent identity (agent_id + api_key). "
        "Call ONLY when you have no token yet: if your MCP config already carries an "
        "Authorization token, you ARE that agent and MUST NOT re-register",
        [] {
            return schemaObject({{"name", schemaString(
                "Human-readable agent name shown to other agents in profiles and member lists")}},
                {"name"});
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
            if (tc.agent != nullptr) {
                Json::Value warning;
                warning["current_agent_id"] = tc.agent->agent_id;
                warning["message"] =
                    "Your request carried a valid token of an existing identity (" +
                    tc.agent->agent_id +
                    "). This NEW pair (agent_id+api_key) will take effect only after your "
                    "operator swaps it into the MCP config; memberships made under the "
                    "current token stay with the current identity.";
                out["identity_warning"] = std::move(warning);
            }
            return out;
        },
        true};
}

}  // namespace voterpool::mcp
