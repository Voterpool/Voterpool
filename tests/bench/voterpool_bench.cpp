// Бенчмарк пропускной способности (NFR-1): RPS инпроцессного dispatch.
// Запуск:
//   voterpool_bench <label> <read|write> [--threads N] [--duration SEC]
//                   [--agents A] [--proposals P]
// Сценарий read  — конкурентные повторяемые операции (search_organizations,
//                  get_proposal, дубликат cast_vote -> -32003): CPU-потолок
//                  движка без влияния диска.
// Сценарий write — слив конечного пула слотов (агент x предложение)
//                  успешным cast_vote с sync-WAL записью: fsync-пол;
//                  --threads 1 даёт RPS на ядро при sync WAL.
// Дефолты скромные (~10 сек замера); тяжёлые нагрузочные прогоны — только
// явно, флагами и на отдельной машине (docs/10 §3.3).
// Вывод: одна JSON-строка в stdout (design D8, change add-rps-benchmarks).
#include "core/Config.h"
#include "core/Metrics.h"
#include "mcp/McpHandler.h"
#include "server/AppContext.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <json/json.h>
#include <spdlog/spdlog.h>

using namespace voterpool;

namespace {

constexpr const char* kWalGauge = "voterpool_rocksdb_wal_synced_total";

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

double percentile(const std::vector<std::int64_t>& v, double p) {
    if (v.empty()) return 0;
    std::vector<std::int64_t> sorted(v);
    std::sort(sorted.begin(), sorted.end());
    size_t idx = static_cast<size_t>(p / 100.0 * (sorted.size() - 1));
    return static_cast<double>(sorted[idx]);
}

struct Options {
    std::string label;
    std::string scenario;
    int threads = 0;  // 0 → hardware_concurrency
    int durationSec = 10;
    int agents = 300;
    int proposals = 100;
};

void printUsage(std::FILE* out) {
    std::fprintf(out,
                 "Usage: voterpool_bench <label> <read|write> [--threads N] [--duration SEC]\n"
                 "                       [--agents A] [--proposals P]\n"
                 "  read  : конкурентный инпроцесс dispatch повторяемых операций (CPU-потолок).\n"
                 "  write : слив пула слотов успешным cast_vote с sync WAL (fsync-пол);\n"
                 "          --threads 1 = RPS на ядро при sync WAL.\n"
                 "  Дефолты: threads=hardware_concurrency, duration=10, agents=300, proposals=100.\n"
                 "  Тяжёлые прогоны (флаги выше дефолтов) — только явно и на отдельной машине\n"
                 "  (docs/10 §3.3).\n");
}

std::optional<Options> parseArgs(int argc, char** argv, bool& help) {
    Options o;
    if (argc > 1 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        help = true;
        return o;
    }
    if (argc < 3) return std::nullopt;
    o.label = argv[1];
    o.scenario = argv[2];
    if (o.scenario != "read" && o.scenario != "write") return std::nullopt;
    for (int i = 3; i < argc; ++i) {
        const std::string flag = argv[i];
        auto value = [&]() -> std::optional<int> {
            if (i + 1 >= argc) return std::nullopt;
            char* end = nullptr;
            long v = std::strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || v <= 0 || v > 1000000) return std::nullopt;
            return static_cast<int>(v);
        };
        if (flag == "--threads") {
            auto v = value();
            if (!v) return std::nullopt;
            o.threads = *v;
        } else if (flag == "--duration") {
            auto v = value();
            if (!v) return std::nullopt;
            o.durationSec = *v;
        } else if (flag == "--agents") {
            auto v = value();
            if (!v) return std::nullopt;
            o.agents = *v;
        } else if (flag == "--proposals") {
            auto v = value();
            if (!v) return std::nullopt;
            o.proposals = *v;
        } else {
            return std::nullopt;
        }
    }
    return o;
}

// Значение gauge из выдачи реестра метрик; строка вида "<name> <value>".
std::int64_t readGauge(const std::string& name) {
    const std::string out = MetricsRegistry::instance().expose();
    const size_t pos = out.find("\n" + name + " ");
    if (pos == std::string::npos) return -1;
    return std::strtoll(out.c_str() + pos + name.size() + 2, nullptr, 10);
}

struct Seed {
    std::vector<AgentContext> agentsCtx;   // [0] — админ, создатель организаций
    std::vector<Json::Value> proposalIds;  // по всем организациям
    double setupSec = 0.0;
};

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error(what); }

Seed seedData(AppContext& app, const Options& opt, bool readMode) {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    Seed s;
    s.agentsCtx.resize(static_cast<size_t>(opt.agents));

    // Сценария write: CONSENT — предложение проходит только последний голос
    // полного круга, поэтому весь пул сливается успешными cast_vote без
    // раннего консенсуса (MAJORITY закрыл бы предложение после Y > T/2 и
    // превратил бы остаток слотов в -32003).
    const char* consensusModel = readMode ? "MAJORITY" : "CONSENT";

    for (int i = 0; i < opt.agents; ++i) {
        const std::string name = "Bench Agent " + std::to_string(i);
        auto r = mcp::dispatchToolForTests(app, nullptr, "register_agent", arg("name", Json::Value(name)));
        if (!r.ok()) fail("register_agent " + name);
        s.agentsCtx[static_cast<size_t>(i)] =
            AgentContext{r.value()["agent_id"].asString(), "NATIVE", false};
    }

    // Не более ~50 предложений на организацию: равномерное распределение и
    // запас параллелизма для per-proposal mutex при умеренном числе join'ов.
    const int numOrgs = std::max(1, std::min((opt.proposals + 49) / 50, opt.agents));
    AgentContext& admin = s.agentsCtx[0];
    for (int orgIdx = 0; orgIdx < numOrgs; ++orgIdx) {
        Json::Value orgArgs;
        orgArgs["name"] = "Bench Org " + std::to_string(orgIdx);
        orgArgs["type"] = "OPEN";
        Json::Value orgCfg;
        orgCfg["consensus_model"] = consensusModel;
        orgCfg["voting_duration_sec"] = 86400;
        orgCfg["power_distribution"] = "EQUAL";
        orgArgs["config"] = std::move(orgCfg);
        auto orgRes = mcp::dispatchToolForTests(app, &admin, "create_organization", orgArgs);
        if (!orgRes.ok()) fail("create_organization " + std::to_string(orgIdx));
        const Json::Value orgId = orgRes.value()["org_id"];

        for (int a = 1; a < opt.agents; ++a) {
            auto j = mcp::dispatchToolForTests(app, &s.agentsCtx[static_cast<size_t>(a)],
                                               "join_organization", arg("org_id", orgId));
            if (!j.ok()) fail("join_organization org=" + std::to_string(orgIdx) + " agent=" + std::to_string(a));
        }

        const int propsForOrg =
            (orgIdx == numOrgs - 1) ? opt.proposals - (opt.proposals / numOrgs) * (numOrgs - 1)
                                    : opt.proposals / numOrgs;
        for (int p = 0; p < propsForOrg; ++p) {
            Json::Value propArgs;
            propArgs["org_id"] = orgId;
            propArgs["title"] = "Bench Proposal " + std::to_string(p);
            propArgs["body"] = "Throughput benchmark filler proposal.";
            propArgs["expires_in_sec"] = 86400;
            auto pr = mcp::dispatchToolForTests(app, &admin, "create_proposal", propArgs);
            if (!pr.ok()) fail("create_proposal " + std::to_string(p));
            s.proposalIds.push_back(pr.value()["proposal_id"]);
        }
    }
    if (static_cast<int>(s.proposalIds.size()) != opt.proposals)
        fail("proposal count mismatch");

    if (readMode && opt.agents > 1) {
        // Пара для повторяемого отказа -32003 в сценарии read.
        Json::Value voteArgs;
        voteArgs["proposal_id"] = s.proposalIds[0];
        voteArgs["decision"] = "YES";
        auto v = mcp::dispatchToolForTests(app, &s.agentsCtx[1], "cast_vote", voteArgs);
        if (!v.ok()) fail("duplicate-vote pair setup");
    }

    s.setupSec = std::chrono::duration<double>(clock::now() - t0).count();
    return s;
}

struct Outcome {
    long ops = 0;
    long okCount = 0;
    long errorCount = 0;
    std::map<int, long> errors;
    std::vector<std::int64_t> ns;
    double elapsedSec = 0.0;
};

void account(Result<Json::Value>& r, Outcome& out) {
    ++out.ops;
    if (r.ok()) {
        ++out.okCount;
    } else {
        ++out.errorCount;
        ++out.errors[r.error().code];
    }
}

Outcome readScenario(AppContext& app, const Options& opt, const Seed& seed) {
    using clock = std::chrono::steady_clock;
    Outcome total;
    const auto deadline = clock::now() + std::chrono::seconds(opt.durationSec);
    const auto t0 = clock::now();
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(opt.threads));

    struct Local {
        long ops = 0, ok = 0, err = 0;
        std::map<int, long> errors;
        std::vector<std::int64_t> ns;
    };
    std::vector<Local> locals(static_cast<size_t>(opt.threads));

    for (int t = 0; t < opt.threads; ++t) {
        workers.emplace_back([&, t] {
            Local& loc = locals[static_cast<size_t>(t)];
            size_t p = static_cast<size_t>(t);
            Json::Value searchArgs = arg("query", Json::Value("bench"));
            while (clock::now() < deadline) {
                auto t0 = clock::now();
                auto r1 = mcp::dispatchToolForTests(app, &seed.agentsCtx[1], "search_organizations", searchArgs);
                auto t1 = clock::now();
                loc.ns.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
                ++loc.ops;
                if (r1.ok()) ++loc.ok; else { ++loc.err; ++loc.errors[r1.error().code]; }

                const Json::Value& pid = seed.proposalIds[p % seed.proposalIds.size()];
                ++p;
                t0 = clock::now();
                auto r2 = mcp::dispatchToolForTests(app, &seed.agentsCtx[1], "get_proposal",
                                                    arg("proposal_id", pid));
                t1 = clock::now();
                loc.ns.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
                ++loc.ops;
                if (r2.ok()) ++loc.ok; else { ++loc.err; ++loc.errors[r2.error().code]; }

                Json::Value dupVote;
                dupVote["proposal_id"] = seed.proposalIds[0];
                dupVote["decision"] = "YES";
                t0 = clock::now();
                auto r3 = mcp::dispatchToolForTests(app, &seed.agentsCtx[1], "cast_vote", dupVote);
                t1 = clock::now();
                loc.ns.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
                ++loc.ops;
                if (r3.ok()) ++loc.ok; else { ++loc.err; ++loc.errors[r3.error().code]; }
            }
        });
    }
    for (auto& w : workers) w.join();
    total.elapsedSec = std::chrono::duration<double>(clock::now() - t0).count();

    for (const auto& loc : locals) {
        total.ops += loc.ops;
        total.okCount += loc.ok;
        total.errorCount += loc.err;
        for (const auto& [code, n] : loc.errors) total.errors[code] += n;
        total.ns.insert(total.ns.end(), loc.ns.begin(), loc.ns.end());
    }
    return total;
}

Outcome writeScenario(AppContext& app, const Options& opt, const Seed& seed,
                      std::int64_t& walSyncedDelta) {
    using clock = std::chrono::steady_clock;
    struct Slot {
        int agentIdx;
        int propIdx;
    };
    std::vector<Slot> slots;
    slots.reserve(static_cast<size_t>(opt.agents) * static_cast<size_t>(opt.proposals));
    for (int a = 0; a < opt.agents; ++a)
        for (int p = 0; p < opt.proposals; ++p) slots.push_back({a, p});
    std::shuffle(slots.begin(), slots.end(), std::mt19937(42));

    std::atomic<size_t> cursor{0};
    app.db->publishStatisticsToRegistry();
    const std::int64_t walBefore = readGauge(kWalGauge);

    Outcome total;
    const auto deadline = clock::now() + std::chrono::seconds(opt.durationSec);
    const auto t0 = clock::now();

    struct Local {
        long ops = 0, ok = 0, err = 0;
        std::map<int, long> errors;
        std::vector<std::int64_t> ns;
    };
    std::vector<Local> locals(static_cast<size_t>(opt.threads));
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(opt.threads));
    for (int t = 0; t < opt.threads; ++t) {
        workers.emplace_back([&, t] {
            Local& loc = locals[static_cast<size_t>(t)];
            while (clock::now() < deadline) {
                const size_t i = cursor.fetch_add(1);
                if (i >= slots.size()) break;
                const Slot s = slots[i];
                Json::Value voteArgs;
                voteArgs["proposal_id"] = seed.proposalIds[static_cast<size_t>(s.propIdx)];
                voteArgs["decision"] = "YES";
                const auto b = clock::now();
                auto r = mcp::dispatchToolForTests(app, &seed.agentsCtx[static_cast<size_t>(s.agentIdx)],
                                                   "cast_vote", voteArgs);
                const auto e = clock::now();
                loc.ns.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(e - b).count());
                ++loc.ops;
                if (r.ok()) ++loc.ok; else { ++loc.err; ++loc.errors[r.error().code]; }
            }
        });
    }
    for (auto& w : workers) w.join();
    total.elapsedSec = std::chrono::duration<double>(clock::now() - t0).count();

    app.db->publishStatisticsToRegistry();
    const std::int64_t walAfter = readGauge(kWalGauge);
    walSyncedDelta = walAfter - walBefore;

    for (const auto& loc : locals) {
        total.ops += loc.ops;
        total.okCount += loc.ok;
        total.errorCount += loc.err;
        for (const auto& [code, n] : loc.errors) total.errors[code] += n;
        total.ns.insert(total.ns.end(), loc.ns.begin(), loc.ns.end());
    }
    return total;
}

double roundMilli(double sec) { return std::round(sec * 1000.0) / 1000.0; }

void emitReport(const Options& opt, const Seed& seed, const Outcome& out,
                const std::optional<std::int64_t>& walSyncedDelta) {
    Json::Value rep;
    rep["label"] = opt.label;
    rep["scenario"] = opt.scenario;
    rep["threads"] = opt.threads;
    rep["agents"] = opt.agents;
    rep["proposals"] = opt.proposals;
    rep["slots"] = static_cast<Json::Int64>(static_cast<long>(opt.agents) * opt.proposals);
    rep["setup_s"] = roundMilli(seed.setupSec);
    rep["duration_s"] = roundMilli(out.elapsedSec);
    rep["ops"] = static_cast<Json::Int64>(out.ops);
    rep["ok"] = static_cast<Json::Int64>(out.okCount);
    Json::Value errs(Json::objectValue);
    for (const auto& [code, n] : out.errors) errs[std::to_string(code)] = static_cast<Json::Int64>(n);
    rep["errors"] = std::move(errs);
    rep["rps"] = out.elapsedSec > 0 ? roundMilli(static_cast<double>(out.ops) / out.elapsedSec) : 0.0;
    rep["ok_rps"] =
        out.elapsedSec > 0 ? roundMilli(static_cast<double>(out.okCount) / out.elapsedSec) : 0.0;
    rep["p50_us"] = percentile(out.ns, 50) / 1000.0;
    rep["p99_us"] = percentile(out.ns, 99) / 1000.0;
    if (walSyncedDelta) rep["wal_synced_delta"] = static_cast<Json::Int64>(*walSyncedDelta);
    std::cout << Json::writeString(Json::StreamWriterBuilder(), rep) << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    // Контракт вывода — ровно одна JSON-строка в stdout: журналирование ядра глушится.
    spdlog::set_level(spdlog::level::off);
    bool help = false;
    auto opt = parseArgs(argc, argv, help);
    if (help) {
        printUsage(stdout);
        return 0;
    }
    if (!opt) {
        printUsage(stderr);
        return 2;
    }
    if (opt->threads <= 0) {
        auto hw = std::thread::hardware_concurrency();
        opt->threads = static_cast<int>(hw > 0 ? hw : 1);
    }

    FixedClock clock;
    AppConfig cfg;
    cfg.storage.path =
        (std::filesystem::temp_directory_path() /
         ("voterpool_rps_" + std::to_string(getpid())))
            .string();
    AppContext app;
    app.config = cfg;
    try {
        app.init(&clock);
    } catch (const std::exception& e) {
        fprintf(stderr, "init failed: %s\n", e.what());
        return 1;
    }

    Seed seed;
    std::optional<std::int64_t> walDelta;
    Outcome outcome;
    try {
        seed = seedData(app, *opt, opt->scenario == "read");
        if (opt->scenario == "read") {
            outcome = readScenario(app, *opt, seed);
        } else {
            std::int64_t delta = 0;
            outcome = writeScenario(app, *opt, seed, delta);
            walDelta = delta;
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "benchmark failed: %s\n", e.what());
        std::error_code ec;
        std::filesystem::remove_all(cfg.storage.path, ec);
        return 1;
    }

    // Жёсткие инварианты процесса: каждая учтённая операция либо успех, либо
    // ошибка ровно одного кода; слот тратится не более одного раза по построению
    // (атомарный fetch_add).
    if (outcome.ops != outcome.okCount + outcome.errorCount) {
        fprintf(stderr, "invariant violated: ops != ok + errors\n");
        return 1;
    }
    if (walDelta && *walDelta != outcome.okCount) {
        if (opt->threads == 1)
            fprintf(stderr,
                    "warning: single-threaded wal_synced_delta (%lld) != ok (%ld); sync-WAL anomaly\n",
                    static_cast<long long>(*walDelta), outcome.okCount);
        // При большей конкурентности расхождение ожидаемо: групповой коммит
        // WAL считает fsync-события, а не записи; delta — информационное поле.
    }

    emitReport(*opt, seed, outcome, walDelta);

    std::error_code ec;
    std::filesystem::remove_all(cfg.storage.path, ec);
    return 0;
}
