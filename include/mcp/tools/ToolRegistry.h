#pragma once

#include "core/Result.h"
#include "server/AppContext.h"
#include "server/AuthProvider.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace voterpool::mcp {

struct ToolContext {
    AppContext& app;
    const AgentContext* agent;
};

using ToolHandler = std::function<Result<Json::Value>(ToolContext&, const Json::Value& args)>;
using SchemaBuilder = std::function<Json::Value()>;

struct ToolArgMeta {
    std::vector<std::pair<std::string, std::string>> props;  // имя → JSON-тип
    std::vector<std::string> names;                          // допустимые имена
};

const ToolArgMeta* findArgMeta(const std::string& toolName);

struct ToolDef {
    const char* name;
    const char* description;
    SchemaBuilder schema;
    ToolHandler handler;
    bool anonymous = false;
};

std::vector<ToolDef>& catalog();

inline void applyDescription(Json::Value& s, const std::string& description) {
    if (!description.empty()) s["description"] = description;
}

inline Json::Value schemaString(std::string description = {}) {
    Json::Value s(Json::objectValue);
    s["type"] = "string";
    applyDescription(s, description);
    return s;
}
inline Json::Value schemaInteger(std::string description = {}) {
    Json::Value s(Json::objectValue);
    s["type"] = "integer";
    applyDescription(s, description);
    return s;
}
inline Json::Value schemaNumber(std::string description = {}) {
    Json::Value s(Json::objectValue);
    s["type"] = "number";
    applyDescription(s, description);
    return s;
}
inline Json::Value schemaBoolean(std::string description = {}) {
    Json::Value s(Json::objectValue);
    s["type"] = "boolean";
    applyDescription(s, description);
    return s;
}
inline Json::Value schemaObjectValue(std::string description = {}) {
    Json::Value s(Json::objectValue);
    s["type"] = "object";
    applyDescription(s, description);
    return s;
}
inline Json::Value schemaArrayOf(const char* itemType, std::string description = {}) {
    Json::Value items(Json::objectValue);
    items["type"] = itemType;
    Json::Value s(Json::objectValue);
    s["type"] = "array";
    s["items"] = std::move(items);
    applyDescription(s, description);
    return s;
}
inline Json::Value schemaEnumOf(std::initializer_list<const char*> values,
                                std::string description = {}) {
    Json::Value variants(Json::arrayValue);
    for (const char* v : values) variants.append(v);
    Json::Value s(Json::objectValue);
    s["type"] = "string";
    s["enum"] = std::move(variants);
    applyDescription(s, description);
    return s;
}

Json::Value schemaObject(std::vector<std::pair<std::string, Json::Value>> properties,
                         std::vector<std::string> required, Json::Value examples = Json::Value(),
                         const std::string& description = {});

}  // namespace voterpool::mcp
