#include "mcp/McpHandler.h"

#include "core/Metrics.h"
#include "mcp/JsonRpcError.h"
#include "mcp/tools/ToolDefs.h"
#include "mcp/tools/ToolRegistry.h"
#include "server/AuthProvider.h"

#include <drogon/HttpResponse.h>
#include <simdjson.h>

namespace voterpool::mcp {
namespace {

const AgentContext* agentFromRequest(const drogon::HttpRequestPtr& req) {
    if (req->attributes()->find("agent_context")) {
        return &(req->attributes()->get<AgentContext>("agent_context"));
    }
    return nullptr;
}

drogon::HttpResponsePtr jsonResponse(const Json::Value& bodyVal, drogon::HttpStatusCode status) {
    auto resp = drogon::HttpResponse::newHttpJsonResponse(bodyVal);
    resp->setStatusCode(status);
    return resp;
}

Json::Value errorBody(const Json::Value& id, int code, const std::string& message, const Json::Value& data) {
    Json::Value out;
    out["jsonrpc"] = "2.0";
    out["id"] = id.isNull() ? Json::Value(Json::nullValue) : id;
    Json::Value err;
    err["code"] = code;
    err["message"] = message;
    if (!data.isNull()) err["data"] = data;
    out["error"] = std::move(err);
    return out;
}

Json::Value resultBody(const Json::Value& id, const std::string& payloadJson) {
    Json::Value out;
    out["jsonrpc"] = "2.0";
    out["id"] = id.isNull() ? Json::Value(Json::nullValue) : id;
    Json::Value result;
    Json::Value content(Json::arrayValue);
    Json::Value item;
    item["type"] = "text";
    item["text"] = payloadJson;
    content.append(item);
    result["content"] = std::move(content);
    out["result"] = std::move(result);
    return out;
}

Json::Value jsonFromValue(simdjson::ondemand::value v);

Json::Value jsonFromObject(simdjson::ondemand::object obj) {
    Json::Value out(Json::objectValue);
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        auto fr = *it;
        std::string key(fr.unescaped_key().value());
        out[std::move(key)] = jsonFromValue(fr.value().value());
    }
    return out;
}

Json::Value jsonFromArray(simdjson::ondemand::array arr) {
    Json::Value out(Json::arrayValue);
    for (auto it = arr.begin(); it != arr.end(); ++it) {
        auto ir = *it;
        out.append(jsonFromValue(ir.value()));
    }
    return out;
}

Json::Value jsonFromValue(simdjson::ondemand::value v) {
    using namespace simdjson;
    Json::Value out;
    ondemand::json_type t = v.type();
    switch (t) {
        case ondemand::json_type::object: {
            out = jsonFromObject(v.get_object());
            break;
        }
        case ondemand::json_type::array: {
            out = jsonFromArray(v.get_array());
            break;
        }
        case ondemand::json_type::string: {
            out = std::string(v.get_string().value());
            break;
        }
        case ondemand::json_type::number: {
            ondemand::number num = v.get_number();
            switch (num.get_number_type()) {
                case ondemand::number_type::unsigned_integer:
                    out = static_cast<Json::Int64>(num.get_uint64());
                    break;
                case ondemand::number_type::signed_integer:
                    out = static_cast<Json::Int64>(num.get_int64());
                    break;
                default:
                    out = num.get_double();
                    break;
            }
            break;
        }
        case ondemand::json_type::boolean: {
            out = static_cast<bool>(v.get_bool());
            break;
        }
        case ondemand::json_type::null: {
            out = Json::Value(Json::nullValue);
            break;
        }
        default:
            break;
    }
    return out;
}

void recordRequestMetrics(const std::string& method, const std::string& name, bool ok) {
    MetricsRegistry::instance().incCounter(
        "voterpool_mcp_requests_total",
        {{"method", method}, {"name", name}, {"outcome", ok ? "ok" : "error"}});
}

void recordErrorMetric(int code) {
    MetricsRegistry::instance().incCounter("voterpool_rpc_errors_total", {{"code", std::to_string(code)}});
}

drogon::HttpResponsePtr respondResult(const Json::Value& id, const std::string& payload) {
    return jsonResponse(resultBody(id, payload), drogon::k200OK);
}

drogon::HttpResponsePtr respondError(const Json::Value& id, const RpcError& err) {
    recordErrorMetric(err.code);
    auto status = err.code == kErrParse ? drogon::k400BadRequest
                  : err.code == -32050 ? drogon::k503ServiceUnavailable
                                       : drogon::k200OK;
    return jsonResponse(errorBody(id, err.code, err.message, err.data), status);
}

Result<Json::Value> dispatchTool(AppContext& app, const AgentContext* agent, const std::string& name,
                                 const Json::Value& args) {
    for (const auto& def : catalog()) {
        if (name != def.name) continue;
        if (!agent && std::string(def.name) != "register_agent") {
            return Result<Json::Value>(RpcError::unauthorized());
        }
        ToolContext tc{app, agent};
        try {
            return def.handler(tc, args);
        } catch (const std::exception& e) {
            spdlog::error("Tool {} failed: {}", name, e.what());
            return RpcError::internal("Unexpected server error");
        }
    }
    return Result<Json::Value>(RpcError{kErrMethodNotFound, "Method not found", Json::Value()});
}

Result<Json::Value> dispatchToolForTestsImpl(AppContext& app, const AgentContext* agent,
                                             const std::string& name, const Json::Value& args) {
    return dispatchTool(app, agent, name, args);
}

Json::Value discoverResponse(AppContext& app) {
    Json::Value out;
    out["protocolVersion"] = app.config.mcp.protocol_version;
    out["capabilities"]["tools"] = Json::Value(Json::objectValue);
    Json::Value ext;
    ext["endpoint"] = "/mcp/events";
    out["extensions"]["io.voterpool/domain-events"] = std::move(ext);
    out["serverInfo"]["name"] = "voterpool";
    out["serverInfo"]["version"] = "1.0.0";
    return out;
}

Json::Value toolsListResponse(AppContext& app) {
    Json::Value out;
    Json::Value tools(Json::arrayValue);
    for (const auto& def : catalog()) {
        Json::Value t;
        t["name"] = def.name;
        t["description"] = def.description;
        t["inputSchema"] = def.schema();
        tools.append(t);
    }
    out["tools"] = std::move(tools);
    out["ttlMs"] = static_cast<Json::Int64>(app.config.mcp.tools_list_cache_ttl_ms);
    out["cacheScope"] = "server";
    return out;
}

}  // namespace

Result<Json::Value> dispatchToolForTests(AppContext& app, const AgentContext* agent,
                                         const std::string& name, const Json::Value& args) {
    return dispatchToolForTestsImpl(app, agent, name, args);
}

void handleMcpPost(AppContext& app, const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>& callback) {
    auto t0 = std::chrono::steady_clock::now();
    std::string body(req->getBody());

    Json::Value root;
    try {
        simdjson::padded_string padded(body);
        simdjson::ondemand::parser parser;
        auto doc = parser.iterate(padded);
        simdjson::ondemand::json_type t = doc.type();
        if (t == simdjson::ondemand::json_type::object) {
            root = jsonFromObject(doc.get_object());
        }
    } catch (const simdjson::simdjson_error&) {
        recordRequestMetrics("tools/call", "_parse", false);
        recordErrorMetric(kErrParse);
        callback(jsonResponse(errorBody(Json::Value(Json::nullValue), kErrParse, "Parse error", Json::Value()),
                              drogon::k400BadRequest));
        return;
    }

    const AgentContext* agent = agentFromRequest(req);

    Json::Value id = (root.isObject() && root.isMember("id")) ? root["id"] : Json::Value(Json::nullValue);

    if (!root.isObject() || !root.isMember("jsonrpc") || !root["jsonrpc"].isString() ||
        root["jsonrpc"].asString() != "2.0" || !root.isMember("method") || !root["method"].isString()) {
        recordErrorMetric(kErrInvalidRequest);
        recordRequestMetrics("tools/call", "_request", false);
        callback(jsonResponse(errorBody(id, kErrInvalidRequest, "Invalid Request", Json::Value()), drogon::k200OK));
        return;
    }
    const std::string method = root["method"].asString();

    if (method == "server/discover") {
        recordRequestMetrics("tools/call", "server/discover", true);
        callback(respondResult(id, Codec::dump(discoverResponse(app))));
        MetricsRegistry::instance().observe(
            "voterpool_http_request_duration_seconds",
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        return;
    }
    if (method == "tools/list") {
        recordRequestMetrics("tools/call", "tools/list", true);
        callback(respondResult(id, Codec::dump(toolsListResponse(app))));
        MetricsRegistry::instance().observe(
            "voterpool_http_request_duration_seconds",
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        return;
    }

    const std::string headerName = req->getHeader("Mcp-Name");

    if (method == "tools/call") {
        const Json::Value& params = root.get("params", Json::Value(Json::objectValue));
        if (!params.isObject() || !params.isMember("name") || !params["name"].isString()) {
            recordErrorMetric(kErrInvalidRequest);
            recordRequestMetrics("tools/call", "_request", false);
            callback(jsonResponse(errorBody(id, kErrInvalidRequest, "Invalid Request", Json::Value()), drogon::k200OK));
            return;
        }
        const std::string toolName = params["name"].asString();
        if (!headerName.empty() && headerName != toolName) {
            recordErrorMetric(kErrInvalidRequest);
            recordRequestMetrics("tools/call", toolName, false);
            callback(jsonResponse(errorBody(id, kErrInvalidRequest, "Invalid Request", Json::Value()), drogon::k200OK));
            return;
        }
        Json::Value args = params.isMember("arguments") && params["arguments"].isObject()
                               ? params["arguments"]
                               : Json::Value(Json::objectValue);
        auto result = dispatchTool(app, agent, toolName, args);
        recordRequestMetrics("tools/call", toolName, result.ok());
        if (!result.ok()) {
            callback(respondError(id, result.error()));
        } else {
            callback(respondResult(id, Codec::dump(result.value())));
        }
    } else {
        const bool known = std::any_of(catalog().begin(), catalog().end(),
                                       [&](const ToolDef& d) { return method == d.name; });
        if (!known) {
            recordErrorMetric(kErrMethodNotFound);
            recordRequestMetrics("direct", method, false);
            callback(jsonResponse(errorBody(id, kErrMethodNotFound, "Method not found", Json::Value()), drogon::k200OK));
            return;
        }
        if (!headerName.empty() && headerName != method) {
            recordErrorMetric(kErrInvalidRequest);
            recordRequestMetrics("direct", method, false);
            callback(jsonResponse(errorBody(id, kErrInvalidRequest, "Invalid Request", Json::Value()), drogon::k200OK));
            return;
        }
        Json::Value args = (root.isMember("params") && root["params"].isObject())
                               ? root["params"]
                               : Json::Value(Json::objectValue);
        auto result = dispatchTool(app, agent, method, args);
        recordRequestMetrics("direct", method, result.ok());
        if (!result.ok()) {
            callback(respondError(id, result.error()));
        } else {
            callback(respondResult(id, Codec::dump(result.value())));
        }
    }

    MetricsRegistry::instance().observe(
        "voterpool_http_request_duration_seconds",
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
}

}  // namespace voterpool::mcp
