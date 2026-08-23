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

TEST(Concurrency, VoteVsTimerCloseExactlyOneCloseAndLateVoteConflicts) {
    constexpr int kIterations = 30;
    for (int iter = 0; iter < kIterations; ++iter) {
        auto h = Harness::create();
        AgentContext creator = h->registerAgent("race1-creator");
        AgentContext voterA = h->registerAgent("race1-a");
        AgentContext voterB = h->registerAgent("race1-b");
        Json::Value orgOut = createOrg(*h, creator, "Race1 Org", "OPEN",
                                       orgConfigArgs("QUORUM_PERCENTAGE", 3600, "EQUAL", 100));
        std::string orgId = orgOut["org_id"].asString();
        for (const auto* a : {&voterA, &voterB}) {
            Json::Value joinArgs;
            joinArgs["org_id"] = orgId;
            ASSERT_FALSE(h->isError(h->call("join_organization", joinArgs, a)));
        }
        Json::Value pOut = createProposal(*h, creator, orgId, "race1-" + std::to_string(iter));
        std::string pid = pOut["proposal_id"].asString();

        std::barrier start(2);
        std::atomic<int> voteOutcome{0};  // 1 = ok, 2 = conflict, 0 = иное
        std::thread voter([&] {
            start.arrive_and_wait();
            auto r = h->app->engine->castVote(voterA.agent_id, pid, VoteDecision::YES);
            if (r.ok()) {
                voteOutcome.store(1);
            } else if (r.error().code == -32003) {
                voteOutcome.store(2);
            }
        });
        std::thread closer([&] {
            start.arrive_and_wait();
            h->app->engine->closeProposalByTimer(orgId, pid);
        });
        voter.join();
        closer.join();

        auto p = h->app->proposals->get(orgId, pid);
        ASSERT_TRUE(p.has_value());
        EXPECT_NE(p->status, ProposalStatus::ACTIVE) << "iter " << iter;

        double sum = 0;
        int rows = 0;
        for (const auto& a : {creator.agent_id, voterA.agent_id, voterB.agent_id}) {
            if (auto v = h->app->votes->get(orgId, pid, a)) {
                ++rows;
                sum += v->power_at_vote;
            }
        }
        EXPECT_DOUBLE_EQ(sum, p->yes_power + p->no_power + p->abstain_power)
            << "iter " << iter << ": счётчики равны сумме зафиксированных голосов";
        EXPECT_EQ(rows, p->voters_count) << "iter " << iter;
        if (voteOutcome.load() == 1) EXPECT_EQ(rows, 1) << "iter " << iter;
        if (voteOutcome.load() == 2) EXPECT_EQ(rows, 0) << "iter " << iter << ": конфликт после закрытия";

        auto late = h->app->engine->castVote(voterB.agent_id, pid, VoteDecision::YES);
        ASSERT_FALSE(late.ok()) << "iter " << iter;
        EXPECT_EQ(late.error().code, -32003) << "iter " << iter << ": опоздавший голос получает Conflict";

        const auto statusBefore = p->status;
        const double yesBefore = p->yes_power;
        ASSERT_TRUE(h->app->engine->closeProposalByTimer(orgId, pid));
        p = h->app->proposals->get(orgId, pid);
        ASSERT_TRUE(p.has_value());
        EXPECT_EQ(p->status, statusBefore) << "iter " << iter << ": повторное закрытие не меняет статус";
        EXPECT_DOUBLE_EQ(p->yes_power, yesBefore) << "iter " << iter << ": ровно одно закрытие";
    }
}
