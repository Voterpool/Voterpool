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

// Хелперы JSON Schema draft 2020-12: каждое свойство inputSchema —
// объект-схема, а не строковый литерал.
inline Json::Value schemaString() {
    Json::Value s(Json::objectValue);
    s["type"] = "string";
    return s;
}
inline Json::Value schemaInteger() {
    Json::Value s(Json::objectValue);
    s["type"] = "integer";
    return s;
}
inline Json::Value schemaNumber() {
    Json::Value s(Json::objectValue);
    s["type"] = "number";
    return s;
}
inline Json::Value schemaBoolean() {
    Json::Value s(Json::objectValue);
    s["type"] = "boolean";
    return s;
}
inline Json::Value schemaObjectValue() {
    Json::Value s(Json::objectValue);
    s["type"] = "object";
    return s;
}
inline Json::Value schemaArrayOf(const char* itemType) {
    Json::Value items(Json::objectValue);
    items["type"] = itemType;
    Json::Value s(Json::objectValue);
    s["type"] = "array";
    s["items"] = std::move(items);
    return s;
}
inline Json::Value schemaEnumOf(std::initializer_list<const char*> values) {
    Json::Value variants(Json::arrayValue);
    for (const char* v : values) variants.append(v);
    Json::Value s(Json::objectValue);
    s["type"] = "string";
    s["enum"] = std::move(variants);
    return s;
}

Json::Value schemaObject(std::vector<std::pair<std::string, Json::Value>> properties,
                         std::vector<std::string> required);

}  // namespace voterpool::mcp
