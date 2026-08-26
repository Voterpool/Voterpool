#include "mcp/tools/ToolRegistry.h"

#include <mutex>

namespace voterpool::mcp {

Json::Value schemaObject(std::vector<std::pair<std::string, Json::Value>> properties,
                         std::vector<std::string> required, Json::Value examples,
                         const std::string& description) {
    Json::Value s;
    s["$schema"] = "https://json-schema.org/draft/2020-12/schema";
    s["type"] = "object";
    if (!description.empty()) s["description"] = description;
    Json::Value props(Json::objectValue);
    for (auto& [name, def] : properties) props[name] = std::move(def);
    s["properties"] = std::move(props);
    if (!required.empty()) {
        Json::Value req(Json::arrayValue);
        for (auto& r : required) req.append(r);
        s["required"] = std::move(req);
    }
    if (examples.isArray() && !examples.empty()) s["examples"] = std::move(examples);
    return s;
}

const ToolArgMeta* findArgMeta(const std::string& toolName) {
    static const std::unordered_map<std::string, ToolArgMeta> meta = [] {
        std::unordered_map<std::string, ToolArgMeta> m;
        for (const auto& d : catalog()) {
            ToolArgMeta am;
            const Json::Value s = d.schema();
            const Json::Value& props = s["properties"];
            for (const auto& k : props.getMemberNames()) {
                am.props.emplace_back(k, props[k].get("type", "").asString());
                am.names.push_back(k);
            }
            m.emplace(d.name, std::move(am));
        }
        return m;
    }();
    auto it = meta.find(toolName);
    return it == meta.end() ? nullptr : &it->second;
}

}  // namespace voterpool::mcp
