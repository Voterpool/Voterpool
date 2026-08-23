#pragma once

#include "core/Result.h"
#include "server/AppContext.h"
#include "server/AuthProvider.h"

#include <functional>
#include <vector>

namespace voterpool::mcp {

struct ToolContext {
    AppContext& app;
    const AgentContext* agent;
};

using ToolHandler = std::function<Result<Json::Value>(ToolContext&, const Json::Value& args)>;
using SchemaBuilder = std::function<Json::Value()>;

struct ToolDef {
    const char* name;
    const char* description;
    SchemaBuilder schema;
    ToolHandler handler;
    bool anonymous = false;
};

std::vector<ToolDef>& catalog();

Json::Value schemaObject(std::vector<std::pair<std::string, Json::Value>> properties,
                         std::vector<std::string> required);

}  // namespace voterpool::mcp
