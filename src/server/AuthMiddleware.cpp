#include "server/AuthMiddleware.h"

#include "core/Metrics.h"
#include "mcp/JsonRpcError.h"
#include "mcp/RequestLog.h"
#include "mcp/tools/ToolRegistry.h"

#include <drogon/HttpResponse.h>
#include <simdjson.h>

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
    std::string metaVersion;
};

// Лёгкое извлечение method и params._meta[protocolVersion] из тела.
// Ошибка парсинга не фатальна: запрос дойдёт до хендлера и получит -32700.
BodyProbe probeBody(const std::string& body) {
    BodyProbe probe;
    try {
        simdjson::dom::parser parser;
        simdjson::dom::element elem = parser.parse(simdjson::padded_string(body));
        try {
            probe.method = std::string(elem.at_pointer("/method").get_string().value());
        } catch (const simdjson::simdjson_error&) {
        }
        try {
            probe.metaVersion =
                std::string(elem.at_pointer("/params/_meta/io.modelcontextprotocol~1protocolVersion")
                                .get_string()
                                .value());
        } catch (const simdjson::simdjson_error&) {
        }
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
        mcpName = req->getHeader("Mcp-Name");
        const BodyProbe probe = probeBody(std::string(req->getBody()));
        mwMethod = probe.method;

        auto rejectProtocol = [&](const std::string& reason) {
            mwLog(false, -32600, mcpName.empty() ? "-" : mcpName);
            mcp::RpcError e{-32600, "Invalid Request", Json::Value()};
            e.data["reason"] = reason;
            e.data["supportedVersions"].append(app_.config.mcp.protocol_version);
            MetricsRegistry::instance().incCounter("voterpool_rpc_errors_total", {{"code", "-32600"}});
            respond(jsonRpcErrorBody(e.code, e.message, e.data, drogon::k200OK));
        };

        // Версия протокола: заголовок приоритетен, при его отсутствии — _meta.
        const std::string effectiveVersion = !hdrVersion.empty() ? hdrVersion : probe.metaVersion;
        if (effectiveVersion != app_.config.mcp.protocol_version) {
            rejectProtocol("Unsupported protocol version");
            return;
        }
        if (!hdrVersion.empty() && !probe.metaVersion.empty() && hdrVersion != probe.metaVersion) {
            rejectProtocol("MCP-Protocol-Version header does not match _meta protocolVersion");
            return;
        }

        // Mcp-Method обязан совпадать с методом тела; для вызовов инструментов
        // дополнительно требуется Mcp-Name.
        if (!probe.method.empty()) {
            if (mcpMethod != probe.method) {
                rejectProtocol("Mcp-Method header does not match request method");
                return;
            }
            const bool toolCall = probe.method == "tools/call" || isToolName(probe.method);
            if (toolCall && mcpName.empty()) {
                rejectProtocol("Mcp-Name header is required for tool calls");
                return;
            }
        } else {
            // Тело не распознано — действуем по заголовкам (хендлер вернёт
            // -32700/-32600 для невалидных тел).
            if (mcpMethod != "tools/call") {
                rejectProtocol("Protocol violation: invalid MCP headers");
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

    auto ctx = auth_.validate(token);
    if (!ctx) {
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
