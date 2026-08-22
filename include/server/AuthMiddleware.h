#pragma once

#include "server/AppContext.h"
#include "server/NativeAuthProvider.h"

#include <drogon/HttpTypes.h>

#include <functional>

namespace drogon {
class HttpRequest;
using HttpRequestPtr = std::shared_ptr<HttpRequest>;
class HttpResponse;
using HttpResponsePtr = std::shared_ptr<HttpResponse>;
}  // namespace drogon

namespace voterpool {

class AuthMiddleware {
public:
    using AdviceCallback = std::function<void(const drogon::HttpResponsePtr&)>;
    using AdviceChainCallback = std::function<void()>;

    explicit AuthMiddleware(AppContext& app) : app_(app), auth_(*app.agents) {}

    void handle(const drogon::HttpRequestPtr& req, AdviceCallback&& respond, AdviceChainCallback&& next);

private:
    AppContext& app_;
    NativeAuthProvider auth_;
};

}  // namespace voterpool
