#pragma once

#include "core/Config.h"
#include "core/IClock.h"
#include "core/Metrics.h"
#include "mcp/McpHandler.h"
#include "server/AppContext.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <random>

namespace voterpool::testing {

inline std::string tempDbDir() {
    static std::atomic<int> counter{0};
    std::string base = std::filesystem::temp_directory_path().string();
    return base + "/voterpool_test_" + std::to_string(getpid()) + "_" +
           std::to_string(counter.fetch_add(1)) + "_" + std::to_string(std::random_device{}());
}

struct Harness {
    std::string dir;
    AppConfig cfg;
    MockClock clock{1700000000};
    std::unique_ptr<AppContext> app;

    static std::shared_ptr<Harness> create(AppConfig overrides = {}) {
        auto h = std::make_shared<Harness>();
        h->cfg = overrides;
        const std::string requested = h->cfg.storage.path;
        const bool looksDefault =
            requested.empty() || requested == "./data/voterpool_db";
        h->dir = looksDefault ? tempDbDir() : requested;
        h->cfg.storage.path = h->dir;
        DbHealth::instance().reset();
        h->app = std::make_unique<AppContext>();
        h->app->config = h->cfg;
        h->app->init(&h->clock);
        return h;
    }

    Json::Value call(const std::string& tool, const Json::Value& args, const AgentContext* agent) {
        auto r = mcp::dispatchToolForTests(*app, agent, tool, args);
        if (!r.ok()) {
            Json::Value err;
            err["__error__"] = r.error().code;
            err["message"] = r.error().message;
            if (r.error().data.isObject()) {
                for (auto const& k : r.error().data.getMemberNames()) err[k] = r.error().data[k];
            }
            return err;
        }
        return r.value();
    }

    bool isError(const Json::Value& v) const { return v.isMember("__error__"); }
    int errorCode(const Json::Value& v) const { return v["__error__"].asInt(); }

    AgentContext registerAgent(const std::string& name) {
        Json::Value args;
        args["name"] = name;
        Json::Value out = call("register_agent", args, nullptr);
        ApiKeyStore::instance().put(out["agent_id"].asString(), out["api_key"].asString());
        return AgentContext{out["agent_id"].asString(), "NATIVE", false};
    }

    ~Harness() {
        if (app) {
            if (app->workers) app->workers->stop();
            app->workers.reset();
            app->engine.reset();
            app->hub.reset();
            if (app->db) app->db->close();
        }
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        MetricsRegistry::instance().resetForTests();
        DbHealth::instance().reset();
    }

    struct ApiKeyStore {
        static ApiKeyStore& instance() {
            static ApiKeyStore s;
            return s;
        }
        void put(const std::string& id, const std::string& key) {
            map_[id] = key;
            last_ = key;
        }
        const std::string& get(const std::string& id) { return map_[id]; }
        const std::string& last() const { return last_; }
        std::map<std::string, std::string> map_;
        std::string last_;
    };
};

}  // namespace voterpool::testing
