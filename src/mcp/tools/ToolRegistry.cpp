#include "mcp/tools/ToolRegistry.h"

namespace voterpool::mcp {

Json::Value schemaObject(std::vector<std::pair<std::string, Json::Value>> properties,
                         std::vector<std::string> required) {
    Json::Value s;
    s["$schema"] = "https://json-schema.org/draft/2020-12/schema";
    s["type"] = "object";
    Json::Value props(Json::objectValue);
    for (auto& [name, def] : properties) props[name] = std::move(def);
    s["properties"] = std::move(props);
    if (!required.empty()) {
        Json::Value req(Json::arrayValue);
        for (auto& r : required) req.append(r);
        s["required"] = std::move(req);
    }
    return s;
}

}  // namespace voterpool::mcp
