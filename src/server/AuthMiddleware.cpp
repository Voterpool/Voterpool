#include "server/AuthMiddleware.h"

#include "mcp/JsonRpcError.h"

#include <drogon/HttpResponse.h>

namespace voterpool {
namespace {

drogon::HttpResponsePtr jsonRpcErrorBody(int code, const std::string& message, const Json::Value& data,
                                         drogon::HttpStatusCode status) {
    Json::Value body;
    body["jsonrpc"] = "2.0";
    body["id"] = Json::Value(Json::nullValue);
    Json::Value err;
    err["code"] = code;
    err["message"] = message;
    if (!data.isNull()) err["data"] = data;
    body["error"] = std::move(err);
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(status);
    return resp;
}

std::string bearerToken(const drogon::HttpRequestPtr& req) {
    std::string auth = req->getHeader("Authorization");
    if (auth.rfind("Bearer ", 0) != 0) return "";
    std::string token = auth.substr(7);
    while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) token.erase(token.begin());
    while (!token.empty() && (token.back() == ' ' || token.back() == '\t' || token.back() == '\r')) token.pop_back();
    return token;
}

}  // namespace

void AuthMiddleware::handle(const drogon::HttpRequestPtr& req, AdviceCallback&& respond,
                            AdviceChainCallback&& next) {
    const std::string& path = req->path();
    const bool isHealth = path == "/health";
    const bool isMetrics = app_.config.metrics.enabled && path == app_.config.metrics.path;

    if (!isHealth && !isMetrics && !DbHealth::instance().healthy()) {
        mcp::RpcError e = mcp::RpcError::overloaded();
        respond(jsonRpcErrorBody(e.code, e.message, e.data, drogon::k503ServiceUnavailable));
        return;
    }
    if (isHealth || isMetrics) {
        next();
        return;
    }

    const bool isMcpPost = path == "/mcp" && req->method() == drogon::Post;
    const bool isSse = path == "/mcp/events" && req->method() == drogon::Get;

    if (isMcpPost) {
        const std::string version = req->getHeader("MCP-Protocol-Version");
        const std::string mcpMethod = req->getHeader("Mcp-Method");
        const std::string mcpName = req->getHeader("Mcp-Name");
        if (version != app_.config.mcp.protocol_version || mcpMethod != "tools/call" || mcpName.empty()) {
            mcp::RpcError e{-32600, "Invalid Request", Json::Value()};
            e.data["reason"] = "Protocol violation: invalid MCP headers";
            MetricsRegistry::instance().incCounter("voterpool_rpc_errors_total", {{"code", "-32600"}});
            respond(jsonRpcErrorBody(e.code, e.message, e.data, drogon::k200OK));
            return;
        }
    }

    std::string token = bearerToken(req);
    if (token.empty()) {
        if (isSse) {
            mcp::RpcError e = mcp::RpcError::unauthorized("Auth token missing or invalid");
            respond(jsonRpcErrorBody(e.code, e.message, e.data, drogon::k401Unauthorized));
            return;
        }
        next();
        return;
    }

    auto ctx = auth_.validate(token);
    if (!ctx) {
        mcp::RpcError e = mcp::RpcError::unauthorized("Auth token missing or invalid");
        respond(jsonRpcErrorBody(e.code, e.message, e.data,
                                 isSse ? drogon::k401Unauthorized : drogon::k200OK));
        return;
    }
    req->attributes()->insert("agent_context", *ctx);
    next();
}

}  // namespace voterpool
