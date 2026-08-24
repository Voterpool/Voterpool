#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

using namespace voterpool;
using namespace voterpool::testing;

namespace {

// Достаёт значение семпла из выдачи /metrics; отсутствие серии = 0.
std::int64_t sampleValue(const std::string& exposition, const std::string& sample) {
    std::size_t pos = exposition.find("\n" + sample + " ");
    if (pos == std::string::npos) return 0;
    pos += sample.size() + 2;
    const std::size_t end = exposition.find('\n', pos);
    return std::stoll(exposition.substr(pos, end - pos));
}

std::string metrics() { return MetricsRegistry::instance().expose(); }

}  // namespace

TEST(MetricsSemantics, TimerExpiredDoesNotCountEarlyExit) {
    auto h = Harness::create();
    AgentContext admin = h->registerAgent("mx-admin");

    // CONSENT: без голосов круг не замкнут, дедлайн даёт ровно EXPIRED
    // (docs/02 §CONSENT: единственный Early-Exit — возражение).
    Json::Value orgOut = createOrg(*h, admin, "Ttl Metrics Org", "OPEN", orgConfigArgs("CONSENT", 600));
    ASSERT_FALSE(h->isError(orgOut));
    std::string orgId = orgOut["org_id"].asString();

    // Второй участник, чтобы предложение имело смысл.
    AgentContext m2 = h->registerAgent("mx-member");
    Json::Value joinArgs;
    joinArgs["org_id"] = orgId;
    ASSERT_FALSE(h->isError(h->call("join_organization", joinArgs, &m2)));

    std::string pid = createProposal(*h, admin, orgId, "expires quietly")["proposal_id"].asString();
    ASSERT_FALSE(pid.empty());

    const std::int64_t before = sampleValue(metrics(), "voterpool_consensus_early_exit_total");
    EXPECT_EQ(before, 0);

    // Таймерное закрытие: дедлайн истёк, голосов нет.
    h->clock.advanceSeconds(601);
    h->app->engine->closeExpired(h->clock.nowSec());

    const std::string out = metrics();
    auto prop = h->app->proposals->get(orgId, pid);
    ASSERT_TRUE(prop.has_value());
    EXPECT_EQ(prop->status, ProposalStatus::EXPIRED);

    EXPECT_EQ(sampleValue(out, "voterpool_consensus_early_exit_total"), before)
        << "timer EXPIRED must not increment early_exit";
    EXPECT_GE(sampleValue(out, "voterpool_proposals_closed_total{final_status=\"EXPIRED\"}"), 1);
}

TEST(MetricsSemantics, VotePathRejectionCountsEarlyExitOnce) {
    auto h = Harness::create();
    AgentContext a1 = h->registerAgent("ve-1");
    AgentContext a2 = h->registerAgent("ve-2");
    AgentContext a3 = h->registerAgent("ve-3");

    // OPEN-организация MAJORITY: T=3, порог простого большинства = 2.
    // После двух голосов NO достижимый Y_max = 0 + (3 - 2) = 1 < 2 → Early-Exit.
    Json::Value orgOut = createOrg(*h, a1, "Early Exit Org", "OPEN", orgConfigArgs("MAJORITY", 600));
    ASSERT_FALSE(h->isError(orgOut));
    std::string orgId = orgOut["org_id"].asString();

    Json::Value joinArgs;
    joinArgs["org_id"] = orgId;
    ASSERT_FALSE(h->isError(h->call("join_organization", joinArgs, &a2)));
    ASSERT_FALSE(h->isError(h->call("join_organization", joinArgs, &a3)));

    std::string pid = createProposal(*h, a1, orgId, "doomed proposal")["proposal_id"].asString();
    ASSERT_FALSE(pid.empty());

    const std::int64_t before = sampleValue(metrics(), "voterpool_consensus_early_exit_total{consensus_model=\"MAJORITY\"}");
    EXPECT_EQ(before, 0);

    Json::Value r1 = vote(*h, a2, pid, "NO");
    EXPECT_EQ(r1["proposal_status"].asString(), "ACTIVE") << "first NO must not close yet";
    EXPECT_EQ(sampleValue(metrics(), "voterpool_consensus_early_exit_total{consensus_model=\"MAJORITY\"}"), before);

    Json::Value r2 = vote(*h, a3, pid, "NO");
    EXPECT_EQ(r2["proposal_status"].asString(), "REJECTED") << "second NO makes PASSED unreachable";
    EXPECT_EQ(sampleValue(metrics(), "voterpool_consensus_early_exit_total{consensus_model=\"MAJORITY\"}"),
              before + 1);
}

TEST(AgentsGauge, ReflectsRegisteredAgentsAndSurvivesRestart) {
    std::string dir = tempDbDir();

    // Фаза 1: сырой AppContext (Harness удалил бы директорию при выходе).
    {
        AppConfig cfg;
        cfg.storage.path = dir;
        DbHealth::instance().reset();
        AppContext ctx;
        ctx.config = cfg;
        ctx.init(nullptr);

        EXPECT_NE(MetricsRegistry::instance().expose().find("# TYPE voterpool_agents_total gauge\n"),
                  std::string::npos);
        EXPECT_EQ(sampleValue(metrics(), "voterpool_agents_total"), 0);

        Json::Value args;
        args["name"] = "seed-agent";
        for (int i = 0; i < 3; ++i) {
            args["name"] = "seed-agent-" + std::to_string(i);
            auto r = mcp::dispatchToolForTests(ctx, nullptr, "register_agent", args);
            ASSERT_TRUE(r.ok());
        }
        EXPECT_EQ(sampleValue(metrics(), "voterpool_agents_total"), 3);
    }

    // Фаза 2: рестарт на непустой базе — gauge корректен с первого скрейпа.
    AppConfig cfg2;
    cfg2.storage.path = dir;
    {
        auto h = Harness::create(cfg2);
        EXPECT_EQ(sampleValue(metrics(), "voterpool_agents_total"), 3)
            << "gauge must be initialized from DB on startup";

        h->registerAgent("ag-4");
        EXPECT_EQ(sampleValue(metrics(), "voterpool_agents_total"), 4);
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
