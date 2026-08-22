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
};

}  // namespace voterpool::testing
