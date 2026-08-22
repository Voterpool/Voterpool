#include "server/VoterpoolApp.h"

#include "mcp/McpHandler.h"
#include "storage/SchemaVersion.h"

#include <drogon/drogon.h>

namespace voterpool {

bool VoterpoolApp::init(IClock* clockOverride) {
    try {
        ctx_.init(clockOverride);
    } catch (const std::exception& e) {
        spdlog::critical("Initialization failed: {}", e.what());
        return false;
    }
    middleware_ = std::make_unique<AuthMiddleware>(ctx_);
    registerRoutes();
    return true;
}

void VoterpoolApp::registerRoutes() {
    drogon::app().registerPreHandlingAdvice(
        [this](const drogon::HttpRequestPtr& req, AuthMiddleware::AdviceCallback&& cb,
               AuthMiddleware::AdviceChainCallback&& next) {
            middleware_->handle(req, std::move(cb), std::move(next));
        });

    drogon::app().registerHandler(
        "/mcp",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            mcp::handleMcpPost(ctx_, req, callback);
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/mcp/events",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            const AgentContext* agent = nullptr;
            if (req->attributes()->find("agent_context")) {
                agent = &(req->attributes()->get<AgentContext>("agent_context"));
            }
            if (!agent) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k401Unauthorized);
                callback(resp);
                return;
            }
            std::vector<std::string> orgIds;
            for (const auto& m : ctx_.orgs->listOrgsOfAgent(agent->agent_id)) {
                if (m.status == MemberStatus::ACTIVE) orgIds.push_back(m.org_id);
            }
            auto resp = drogon::HttpResponse::newAsyncStreamResponse(
                [this, orgIds, agentId = agent->agent_id](drogon::ResponseStreamPtr stream) {
                    stream->send(": connected\n\n");
                    ctx_.hub->registerStreams(orgIds, agentId, std::move(stream));
                },
                true);
            resp->setStatusCode(drogon::k200OK);
            resp->setContentTypeString("text/event-stream");
            resp->addHeader("Cache-Control", "no-cache");
            resp->addHeader("X-Accel-Buffering", "no");
            callback(resp);
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/health",
        [](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(DbHealth::instance().healthy() ? drogon::k200OK : drogon::k503ServiceUnavailable);
            resp->setBody(DbHealth::instance().healthy() ? "OK" : "Storage backend unavailable");
            callback(resp);
        },
        {drogon::Get});

    drogon::app().registerHandler(
        ctx_.config.metrics.path,
        [this](const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setStatusCode(drogon::k200OK);
            resp->setContentTypeString("text/plain; version=0.0.4");
            resp->setBody(MetricsRegistry::instance().expose());
            callback(resp);
        },
        {drogon::Get});
}

void VoterpoolApp::startWorkers() {
    ctx_.workers = std::make_unique<Workers>(
        ctx_.config, *ctx_.clock, *ctx_.hub,
        [this](std::int64_t nowSec) { ctx_.engine->closeExpired(nowSec); });
    ctx_.workers->start();
}

int VoterpoolApp::run() {
    auto drainAndQuit = [this]() {
        ctx_.hub->shutdownAll();
        if (ctx_.workers) ctx_.workers->stop();
        drogon::app().getLoop()->runAfter(0.2, [] { drogon::app().quit(); });
    };
    drogon::app()
        .setTermSignalHandler(drainAndQuit)
        .setIntSignalHandler(drainAndQuit)
        .setLogLevel(trantor::Logger::kWarn)
        .addListener(ctx_.config.server.host, static_cast<uint16_t>(ctx_.config.server.port))
        .setThreadNum(static_cast<size_t>(ctx_.config.server.threads_num));
    spdlog::info("Voterpool Engine started on {}:{}", ctx_.config.server.host, ctx_.config.server.port);
    drogon::app().run();
    return 0;
}

void VoterpoolApp::finalizeShutdown() {
    if (!ctx_.workers) return;
    ctx_.workers->stop();
    ctx_.workers.reset();
    if (ctx_.hub) ctx_.hub->shutdownAll();
    if (ctx_.db) ctx_.db->close();
    spdlog::info("Shutdown complete");
}

}  // namespace voterpool
