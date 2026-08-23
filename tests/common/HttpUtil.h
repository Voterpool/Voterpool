#pragma once

#include <json/json.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace voterpool::testing {

struct HttpResponse {
    int status = 0;
    std::string body;
    std::map<std::string, std::string> headers;
};

class HttpUtil {
public:
    static HttpResponse request(const std::string& host, int port, const std::string& method,
                                const std::string& path,
                                const std::vector<std::pair<std::string, std::string>>& headers,
                                const std::string& body, int timeoutMs = 5000);

    static HttpResponse get(const std::string& host, int port, const std::string& path,
                            const std::vector<std::pair<std::string, std::string>>& headers = {},
                            int timeoutMs = 5000) {
        return request(host, port, "GET", path, headers, "", timeoutMs);
    }

    static HttpResponse postJson(const std::string& host, int port, const std::string& path,
                                 const Json::Value& body,
                                 const std::vector<std::pair<std::string, std::string>>& headers = {},
                                 int timeoutMs = 5000);

    static bool waitForHttp(const std::string& host, int port, const std::string& path, int timeoutMs = 10000);

    // Заголовки MCP 2026-07-28: Mcp-Method = фактический JSON-RPC method;
    // Mcp-Name обязателен только для вызовов инструментов.
    static std::vector<std::pair<std::string, std::string>> mcpHeaders(const std::string& method,
                                                                       const std::string& toolName = "") {
        std::vector<std::pair<std::string, std::string>> headers = {
            {"MCP-Protocol-Version", "2026-07-28"}, {"Mcp-Method", method}};
        if (!toolName.empty()) headers.push_back({"Mcp-Name", toolName});
        return headers;
    }

    // Профиль «стоковый клиент»: версия только в params._meta, без
    // MCP-Protocol-Version и без Mcp-Name. Принимает объект params.
    static void setStockClientMeta(Json::Value& params) {
        params["_meta"]["io.modelcontextprotocol/protocolVersion"] = "2026-07-28";
    }
};

}  // namespace voterpool::testing
