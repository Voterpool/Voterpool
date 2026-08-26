#include "server/AuthMiddleware.h"

#include "core/Metrics.h"
#include "mcp/JsonRpcError.h"
#include "mcp/RequestLog.h"
#include "mcp/tools/ToolRegistry.h"

#include <drogon/HttpResponse.h>
#include <simdjson.h>

#include <algorithm>

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

struct BodyProbe {
    std::string method;
    // Версия протокола: три источника по приоритету — заголовок,
    // params.protocolVersion (переговоры initialize), params._meta.
    std::string metaVersion;
    std::string bodyVersion;
    std::string toolName;
};

// Лёгкое извлечение method, версии протокола и имени инструмента из тела.
// Ошибка парсинга не фатальна: запрос дойдёт до хендлера и получит -32700.
BodyProbe probeBody(const std::string& body) {
    BodyProbe probe;
    try {
        simdjson::dom::parser parser;
        simdjson::dom::element elem = parser.parse(simdjson::padded_string(body));
        auto getString = [&](const char* pointer) -> std::string {
            try {
                return std::string(elem.at_pointer(pointer).get_string().value());
            } catch (const simdjson::simdjson_error&) {
                return {};
            }
        };
        probe.method = getString("/method");
        probe.metaVersion =
            getString("/params/_meta/io.modelcontextprotocol~1protocolVersion");
        probe.bodyVersion = getString("/params/protocolVersion");
        probe.toolName = getString("/params/name");
    } catch (const simdjson::simdjson_error&) {
    }
    return probe;
}

bool isToolName(const std::string& name) {
    const auto& cat = mcp::catalog();
    return std::any_of(cat.begin(), cat.end(), [&](const mcp::ToolDef& d) { return name == d.name; });
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
    const auto mwT0 = std::chrono::steady_clock::now();
    std::string mwRequestId;
    std::string mwMethod;
    std::string mcpName;
    auto mwLog = [&](bool ok, int errorCode, const std::string& tool) {
        if (!isMcpPost) return;
        mcp::RequestLogContext c;
        c.requestId = mwRequestId.empty() ? (mwRequestId = mcp::nextRequestId()) : mwRequestId;
        c.ok = ok;
        c.errorCode = errorCode;
        c.t0 = mwT0;
        c.method = mwMethod;
        c.tool = tool;
        mcp::emitRequestLog(c);
    };

    if (isMcpPost) {
        const std::string hdrVersion = req->getHeader("MCP-Protocol-Version");
        const std::string mcpMethod = req->getHeader("Mcp-Method");
        const std::string hdrToolName = req->getHeader("Mcp-Name");
        const BodyProbe probe = probeBody(std::string(req->getBody()));
        mwMethod = probe.method;

        auto rejectProtocol = [&](const std::string& reason) {
            mwLog(false, -32600, mcpName.empty() ? "-" : mcpName);
            mcp::RpcError e{-32600, "Invalid Request", Json::Value()};
            e.data["reason"] = reason;
            for (const auto& v : app_.config.mcp.supported_versions)
                e.data["supportedVersions"].append(v);
            MetricsRegistry::instance().incCounter("voterpool_rpc_errors_total", {{"code", "-32600"}});
            respond(jsonRpcErrorBody(e.code, e.message, e.data, drogon::k200OK));
        };

       
        if (!probe.method.empty() && probe.method.rfind("notifications/", 0) != 0) {
            const bool toolCall = probe.method == "tools/call" || isToolName(probe.method);
           
            std::string bodyToolName;
            if (toolCall) bodyToolName = probe.method == "tools/call" ? probe.toolName : probe.method;
            mcpName = !hdrToolName.empty() ? hdrToolName : bodyToolName;

            if (!mcpMethod.empty() && mcpMethod != probe.method) {
                rejectProtocol("Mcp-Method header does not match request method");
                return;
            }
            if (toolCall && !hdrToolName.empty() && !bodyToolName.empty() &&
                hdrToolName != bodyToolName) {
                rejectProtocol("Mcp-Name header does not match requested tool");
                return;
            }

            if (!hdrVersion.empty() && !probe.metaVersion.empty() &&
                hdrVersion != probe.metaVersion) {
                rejectProtocol(
                    "MCP-Protocol-Version header does not match _meta protocolVersion");
                return;
            }
            if (!hdrVersion.empty() && !probe.bodyVersion.empty() &&
                hdrVersion != probe.bodyVersion) {
                rejectProtocol(
                    "MCP-Protocol-Version header does not match params.protocolVersion");
                return;
            }
            if (!probe.bodyVersion.empty() && !probe.metaVersion.empty() &&
                probe.bodyVersion != probe.metaVersion) {
                rejectProtocol("params.protocolVersion does not match _meta protocolVersion");
                return;
            }
            const std::string& declared =
                !hdrVersion.empty()
                    ? hdrVersion
                    : (!probe.bodyVersion.empty() ? probe.bodyVersion : probe.metaVersion);
            // initialize согласует версию ответом (эхо/fallback), а не отказом.
            const auto& supported = app_.config.mcp.supported_versions;
            if (!declared.empty() && probe.method != "initialize" &&
                std::find(supported.begin(), supported.end(), declared) == supported.end()) {
                rejectProtocol("Unsupported protocol version");
                return;
            }
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

    auto ctx = app_.identity->resolveToken(token);
    if (!ctx) {
        const bool isMcpGet = path == "/mcp" && req->method() == drogon::Get;
        if (isMcpGet) {
            next();
            return;
        }
        mwLog(false, -32001, mcpName.empty() ? std::string("-") : mcpName);
        mcp::RpcError e = mcp::RpcError::unauthorized("Auth token missing or invalid");
        respond(jsonRpcErrorBody(e.code, e.message, e.data,
                                 isSse ? drogon::k401Unauthorized : drogon::k200OK));
        return;
    }
    req->attributes()->insert("agent_context", *ctx);
    next();
}

}  // namespace voterpool
