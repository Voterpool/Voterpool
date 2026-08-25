// Микро-бенчмарк горячего пути: p50/p99 операций на портах Directory/Identity.
// Запуск: voterpool_hot_path_bench <label> [iterations]
// Вывод: одна JSON-строка с перцентилями в наносекундах.
#include "core/Config.h"
#include "mcp/McpHandler.h"
#include "server/AppContext.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <json/json.h>

using namespace voterpool;

namespace {

class FixedClock : public IClock {
public:
    std::int64_t nowSec() const override { return 1700000000; }
    std::int64_t nowMilli() const override { return 1700000000000LL; }
};

Json::Value arg(const std::string& key, const Json::Value& value) {
    Json::Value a(Json::objectValue);
    a[key] = value;
    return a;
}

double percentile(std::vector<std::int64_t>& v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    size_t idx = static_cast<size_t>(p / 100.0 * (v.size() - 1));
    return static_cast<double>(v[idx]);
}

struct Sample {
    const char* op;
    std::vector<std::int64_t> ns;
};

void report(const char* label, std::vector<Sample>& samples, int iterations) {
    Json::Value out;
    out["label"] = label;
    out["iterations"] = iterations;
    for (auto& s : samples) {
        Json::Value d(Json::objectValue);
        d["p50_ns"] = percentile(s.ns, 50);
        d["p99_ns"] = percentile(s.ns, 99);
        out[s.op] = std::move(d);
    }
    std::cout << Json::writeString(Json::StreamWriterBuilder(), out) << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    const char* label = argc > 1 ? argv[1] : "unlabeled";
    const int iterations = argc > 2 ? std::atoi(argv[2]) : 3000;

    FixedClock clock;
    AppConfig cfg;
    cfg.storage.path =
        (std::filesystem::temp_directory_path() / ("voterpool_bench_" + std::to_string(getpid())))
            .string();
    AppContext app;
    app.config = cfg;
    try {
        app.init(&clock);
    } catch (const std::exception& e) {
        fprintf(stderr, "init failed: %s\n", e.what());
        return 1;
    }

    // Сценарий: 3 агента, OPEN-организация, двое вступают, предложение.
    auto reg = [&](const char* name) {
        auto r = mcp::dispatchToolForTests(app, nullptr, "register_agent", arg("name", Json::Value(name)));
        return r.ok() ? r.value() : Json::Value();
    };
    Json::Value a1 = reg("Bench Alice"), a2 = reg("Bench Bob"), a3 = reg("Bench Carol");
    AgentContext c1{a1["agent_id"].asString(), "NATIVE", false};
    AgentContext c2{a2["agent_id"].asString(), "NATIVE", false};
    AgentContext c3{a3["agent_id"].asString(), "NATIVE", false};

    Json::Value orgArgs;
    orgArgs["name"] = "Bench Org";
    orgArgs["type"] = "OPEN";
    Json::Value orgCfg;
    orgCfg["consensus_model"] = "MAJORITY";
    orgCfg["voting_duration_sec"] = 3600;
    orgCfg["power_distribution"] = "EQUAL";
    orgArgs["config"] = std::move(orgCfg);
    auto orgRes = mcp::dispatchToolForTests(app, &c1, "create_organization", orgArgs);
    if (!orgRes.ok()) {
        fprintf(stderr, "create_organization failed\n");
        return 1;
    }
    const Json::Value orgId = orgRes.value()["org_id"];

    auto joinYes = mcp::dispatchToolForTests(app, &c2, "join_organization", arg("org_id", orgId));
    mcp::dispatchToolForTests(app, &c3, "join_organization", arg("org_id", orgId));
    if (!joinYes.ok()) {
        fprintf(stderr, "join failed\n");
        return 1;
    }

    Json::Value propArgs;
    propArgs["org_id"] = orgId;
    propArgs["title"] = "Bench proposal";
    propArgs["body"] = "Should we keep benchmarking?";
    propArgs["expires_in_sec"] = 3600;
    auto propRes = mcp::dispatchToolForTests(app, &c1, "create_proposal", propArgs);
    if (!propRes.ok()) {
        fprintf(stderr, "create_proposal failed\n");
        return 1;
    }
    const Json::Value propId = propRes.value()["proposal_id"];

    // Первый голос c2 — успешный; далее в цикле голосуем повторно тем же
    // агентом: полный путь до -32003 (lookup + membership + lock),
    // детерминированный отказ без мутаций состояния.
    Json::Value voteArgs;
    voteArgs["proposal_id"] = propId;
    voteArgs["decision"] = "YES";
    mcp::dispatchToolForTests(app, &c2, "cast_vote", voteArgs);

    Json::Value dupVote = voteArgs;

    std::vector<Sample> samples = {{"search_organizations", {}},
                                   {"get_agent", {}},
                                   {"get_proposal", {}},
                                   {"cast_vote_duplicate", {}},
                                   {"ttl_tick_noop", {}}};

    for (int i = 0; i < iterations; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        mcp::dispatchToolForTests(app, &c3, "search_organizations", arg("query", Json::Value("bench")));
        auto t1 = std::chrono::steady_clock::now();
        samples[0].ns.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

        t0 = std::chrono::steady_clock::now();
        mcp::dispatchToolForTests(app, &c1, "get_agent", arg("agent_id", a2["agent_id"]));
        t1 = std::chrono::steady_clock::now();
        samples[1].ns.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

        t0 = std::chrono::steady_clock::now();
        mcp::dispatchToolForTests(app, &c1, "get_proposal", arg("proposal_id", propId));
        t1 = std::chrono::steady_clock::now();
        samples[2].ns.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

        t0 = std::chrono::steady_clock::now();
        mcp::dispatchToolForTests(app, &c2, "cast_vote", dupVote);
        t1 = std::chrono::steady_clock::now();
        samples[3].ns.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

        // Полный тик TTL-воркера без истёкших предложений: стоимость
        // скана active_proposals.
        t0 = std::chrono::steady_clock::now();
        app.engine->closeExpired(clock.nowSec() + 1000000);
        t1 = std::chrono::steady_clock::now();
        samples[4].ns.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    report(label, samples, iterations);
    std::error_code ec;
    std::filesystem::remove_all(cfg.storage.path, ec);
    return 0;
}
