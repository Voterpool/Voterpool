#pragma once

#include "server/AppContext.h"
#include "server/AuthProvider.h"

#include <drogon/HttpTypes.h>

#include <functional>

namespace drogon {
class HttpRequest;
using HttpRequestPtr = std::shared_ptr<HttpRequest>;
class HttpResponse;
using HttpResponsePtr = std::shared_ptr<HttpResponse>;
}  // namespace drogon

namespace voterpool::mcp {

void handleMcpPost(AppContext& app, const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>& callback);

Result<Json::Value> dispatchToolForTests(AppContext& app, const AgentContext* agent,
                                         const std::string& name, const Json::Value& args);

}  // namespace voterpool::mcp
