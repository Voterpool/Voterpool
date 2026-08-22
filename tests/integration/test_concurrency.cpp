#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

#include <barrier>
#include <set>
#include <thread>

using namespace voterpool;
using namespace voterpool::testing;

TEST(Concurrency, ParallelVotesLoseNoUpdates) {
    constexpr int kAgents = 12;
    constexpr int kFirstWave = 6;
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("conc-creator");
    Json::Value orgOut = createOrg(*h, creator, "Conc Org", "OPEN",
                                   orgConfigArgs("QUORUM_PERCENTAGE", 3600, "EQUAL", 100));
    std::string orgId = orgOut["org_id"].asString();

    std::vector<AgentContext> agents;
    for (int i = 0; i < kAgents; ++i) {
        AgentContext a = h->registerAgent("conc-voter-" + std::to_string(i));
        Json::Value joinArgs;
        joinArgs["org_id"] = orgId;
        ASSERT_FALSE(h->isError(h->call("join_organization", joinArgs, &a)));
        agents.push_back(a);
    }
    Json::Value pOut = createProposal(*h, creator, orgId, "parallel");
    std::string pid = pOut["proposal_id"].asString();

    std::atomic<int> successes{0};
    std::barrier barrier(kFirstWave + 1);
    std::vector<std::thread> threads;
    for (int i = 0; i < kFirstWave; ++i) {
        threads.emplace_back([&, i] {
            barrier.arrive_and_wait();
            Json::Value r = vote(*h, agents[i], pid, "YES");
            if (!h->isError(r)) successes.fetch_add(1);
        });
    }
    barrier.arrive_and_wait();
    for (auto& t : threads) t.join();

    EXPECT_EQ(successes.load(), kFirstWave)
        << "quorum 100% при T=13 недостижим первой волной — закрытие исключено";
    auto p = h->app->proposals->get(orgId, pid);
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->status, ProposalStatus::ACTIVE);
    EXPECT_DOUBLE_EQ(p->yes_power, static_cast<double>(kFirstWave));
    EXPECT_EQ(p->voters_count, kFirstWave);

    int storedYes = 0;
    double sumPowers = 0;
    for (const auto& a : agents) {
        if (auto v = h->app->votes->get(orgId, pid, a.agent_id)) {
            if (v->decision == VoteDecision::YES) ++storedYes;
            sumPowers += v->power_at_vote;
        }
    }
    EXPECT_EQ(storedYes, kFirstWave);
    EXPECT_DOUBLE_EQ(sumPowers, p->yes_power + p->no_power + p->abstain_power);

    std::atomic<int> secondWave{0};
    threads.clear();
    std::barrier barrier2(kAgents - kFirstWave + 1);
    for (int i = kFirstWave; i < kAgents; ++i) {
        threads.emplace_back([&, i] {
            barrier2.arrive_and_wait();
            Json::Value r = vote(*h, agents[i], pid, "YES");
            if (!h->isError(r)) secondWave.fetch_add(1);
            Json::Value dup = vote(*h, agents[i], pid, "NO");
            EXPECT_TRUE(h->isError(dup));
        });
    }
    barrier2.arrive_and_wait();
    for (auto& t : threads) t.join();

    p = h->app->proposals->get(orgId, pid);
    ASSERT_TRUE(p.has_value());
    const double applied = p->yes_power + p->no_power + p->abstain_power;
    EXPECT_DOUBLE_EQ(applied, static_cast<double>(successes.load() + secondWave.load()));
    EXPECT_EQ(successes.load() + secondWave.load(), kAgents)
        << "ровно один успешный голос на агента, ноль потерянных обновлений";
    EXPECT_EQ(p->status, ProposalStatus::ACTIVE)
        << "кворум 100% недостижим без голоса создателя — предложение живо";
}

TEST(Concurrency, TimerCloseRacesVoteUnderSameLock) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("race-a");
    AgentContext voter = h->registerAgent("race-b");
    Json::Value orgOut = createOrg(*h, creator, "Race Org", "OPEN",
                                   orgConfigArgs("QUORUM_PERCENTAGE", 600, "EQUAL", 100));
    std::string orgId = orgOut["org_id"].asString();
    h->call("join_organization", [&] { Json::Value a; a["org_id"] = orgId; return a; }(), &voter);
    Json::Value pOut = createProposal(*h, creator, orgId, "racy");
    std::string pid = pOut["proposal_id"].asString();

    std::atomic<int> voteResults{0};
    std::thread voterThread([&] {
        for (int i = 0; i < 50; ++i) {
            Json::Value r = vote(*h, voter, pid, "YES");
            if (h->isError(r)) break;
            ++voteResults;
        }
    });

    h->clock.advanceSeconds(700);
    for (int i = 0; i < 20 && voteResults.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    h->app->engine->closeExpired(h->clock.nowSec());
    voterThread.join();

    auto p = h->app->proposals->get(orgId, pid);
    ASSERT_TRUE(p.has_value());
    EXPECT_NE(p->status, ProposalStatus::ACTIVE);

    double sum = 0;
    for (const auto& a : {creator.agent_id, voter.agent_id}) {
        if (auto v = h->app->votes->get(orgId, pid, a)) sum += v->power_at_vote;
    }
    EXPECT_DOUBLE_EQ(sum, p->yes_power + p->no_power + p->abstain_power);
}
