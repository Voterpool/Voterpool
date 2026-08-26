#include "server/VoterpoolApp.h"

#include "core/Metrics.h"
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
            // Draining (background-workers): после сигнала новые HTTP/MCP
            // соединения не принимаются — мгновенный 503 без диспетчеризации.
            // /health и /metrics живут до выхода процесса.
            const std::string& path = req->path();
            const bool bypass =
                path == "/health" || (ctx_.config.metrics.enabled && path == ctx_.config.metrics.path);
            if (!bypass && draining_.load(std::memory_order_acquire)) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k503ServiceUnavailable);
                resp->setBody("Server is shutting down");
                cb(resp);
                return;
            }
            middleware_->handle(req, std::move(cb), std::move(next));
        });

    drogon::app().registerHandler(
        "/mcp",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            mcp::handleMcpPost(ctx_, req, callback);
        },
        {drogon::Post});

  
    auto methodNotAllowed = [](const drogon::HttpRequestPtr&,
                               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k405MethodNotAllowed);
        resp->addHeader("Allow", "POST");
        callback(resp);
    };
    // GET /mcp — standalone keepalive-поток для клиентов ревизий 2025-*:
    // после handshake они открывают GET и считают не-200 фатальной ошибкой.
    // Поток данных не несёт (только heartbeat из SseHub), анонимен;
    // невалидный токен middleware не отклоняет (см. AuthMiddleware).
    drogon::app().registerHandler(
        "/mcp",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            const AgentContext* agent = nullptr;
            if (req->attributes()->find("agent_context")) {
                agent = &(req->attributes()->get<AgentContext>("agent_context"));
            }
            MetricsRegistry::instance().incCounter("voterpool_mcp_get_streams_total",
                                                   {{"outcome", "opened"}});
            spdlog::info("GET /mcp keep-alive stream opened (agent {})",
                         agent ? agent->agent_id : "-");
            auto resp = drogon::HttpResponse::newAsyncStreamResponse(
                [this](drogon::ResponseStreamPtr stream) {
                    stream->send(": connected\n\n");
                    ctx_.events->registerKeepAlive(std::move(stream));
                },
                true);
            resp->setStatusCode(drogon::k200OK);
            resp->setContentTypeString("text/event-stream");
            resp->addHeader("Cache-Control", "no-cache");
            resp->addHeader("Connection", "keep-alive");
            resp->addHeader("X-Accel-Buffering", "no");
            callback(resp);
        },
        {drogon::Get});

    // DELETE /mcp остаётся 405: сервер stateless, завершать нечего; SDK-клиенты
    // толерантны к 405 на terminateSession.
    drogon::app().registerHandler("/mcp", methodNotAllowed, {drogon::Delete});

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
            for (const auto& m : ctx_.identity->listOrgsOfAgent(agent->agent_id)) {
                if (m.status == MemberStatus::ACTIVE) orgIds.push_back(m.org_id);
            }
            auto resp = drogon::HttpResponse::newAsyncStreamResponse(
                [this, orgIds, agentId = agent->agent_id](drogon::ResponseStreamPtr stream) {
                    stream->send(": connected\n\n");
                    ctx_.events->subscribeAllOrgs(orgIds, agentId, std::move(stream));
                },
                true);
            resp->setStatusCode(drogon::k200OK);
            resp->setContentTypeString("text/event-stream");
            resp->addHeader("Cache-Control", "no-cache");
            resp->addHeader("Connection", "keep-alive");
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
            if (ctx_.db && ctx_.db->isOpen()) {
                ctx_.db->publishStatisticsToRegistry();
                ctx_.db->publishBucketHistogram();
            }
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
        ctx_.config, *ctx_.clock, *ctx_.events,
        [this](std::int64_t nowSec) { ctx_.engine->closeExpired(nowSec); });
    ctx_.workers->start();
}

int VoterpoolApp::run() {
    auto drainAndQuit = [this]() {
        // CAS делает повторный SIGTERM/SIGINT во время остановки безопасным.
        bool expected = false;
        if (!draining_.compare_exchange_strong(expected, true)) return;
        ctx_.events->shutdownAll();
        if (ctx_.workers) ctx_.workers->stop();
        drogon::app().getLoop()->runAfter(0.2, [] { drogon::app().quit(); });
    };
    // Транспортные параметры применяются к листенеру (configuration):
    // TLS-листенер при ssl.enabled, лимит тела и таймаут простоя — всегда.
    auto& server = ctx_.config.server;
    drogon::app()
        .setTermSignalHandler(drainAndQuit)
        .setIntSignalHandler(drainAndQuit)
        .setLogLevel(trantor::Logger::kWarn)
        .setClientMaxBodySize(static_cast<size_t>(server.max_request_body_size))
        .setIdleConnectionTimeout(static_cast<size_t>(server.request_timeout_sec));
    if (server.ssl.enabled) {
        drogon::app().setSSLFiles(server.ssl.cert_path, server.ssl.key_path);
        drogon::app().addListener(server.host, static_cast<uint16_t>(server.port), true,
                                  server.ssl.cert_path, server.ssl.key_path);
    } else {
        drogon::app().addListener(server.host, static_cast<uint16_t>(server.port));
    }
    drogon::app().setThreadNum(static_cast<size_t>(server.threads_num));
    spdlog::info("Voterpool Engine started on {}:{} ({})", server.host, server.port,
                 server.ssl.enabled ? "TLS" : "plaintext");
    drogon::app().run();
    return 0;
}

void VoterpoolApp::finalizeShutdown() {
    if (!ctx_.workers) return;
    ctx_.workers->stop();
    ctx_.workers.reset();
    if (ctx_.events) ctx_.events->shutdownAll();
    if (ctx_.db) ctx_.db->close();
    spdlog::info("Shutdown complete");
}

}  // namespace voterpool
