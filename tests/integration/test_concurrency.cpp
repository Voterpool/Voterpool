#include "consensus/IConsensusModel.h"
#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

#include <barrier>
#include <mutex>
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

TEST(Concurrency, ConcurrentJoinsRespectLimitsAndTotals) {
    constexpr int kThreads = 12;
    constexpr int kLimit = 5;
    constexpr int kIterations = 20;
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("joinlim-creator");
    std::vector<AgentContext> agents;
    for (int i = 0; i < kThreads; ++i) {
        agents.push_back(h->registerAgent("joinlim-" + std::to_string(i)));
    }

    for (int iter = 0; iter < kIterations; ++iter) {
        Json::Value args;
        args["name"] = "JoinLim Org " + std::to_string(iter);
        args["type"] = "OPEN";
        args["joins_per_day_limit"] = kLimit;
        args["config"] = orgConfigArgs("MAJORITY", 3600);
        Json::Value orgOut = h->call("create_organization", args, &creator);
        ASSERT_FALSE(h->isError(orgOut)) << "iter " << iter;
        std::string orgId = orgOut["org_id"].asString();

        std::barrier start(kThreads + 1);
        std::atomic<int> successes{0};
        std::atomic<int> rejected{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < kThreads; ++i) {
            threads.emplace_back([&, i] {
                Json::Value joinArgs;
                joinArgs["org_id"] = orgId;
                start.arrive_and_wait();
                Json::Value r = h->call("join_organization", joinArgs, &agents[i]);
                if (!h->isError(r)) {
                    successes.fetch_add(1);
                } else if (h->errorCode(r) == -32005) {
                    rejected.fetch_add(1);
                }
            });
        }
        start.arrive_and_wait();
        for (auto& t : threads) t.join();

        EXPECT_EQ(successes.load(), kLimit) << "iter " << iter << ": ровно K вступлений";
        EXPECT_EQ(successes.load() + rejected.load(), kThreads) << "iter " << iter << ": остальные -32005";

        EXPECT_EQ(h->app->indexes->getJoinLimit(orgId, h->clock.nowSec()), kLimit)
            << "iter " << iter << ": счётчик равен числу фактических вступлений";
        auto org = h->app->orgs->get(orgId);
        ASSERT_TRUE(org.has_value());
        EXPECT_DOUBLE_EQ(org->total_voting_power, 1.0 + static_cast<double>(kLimit))
            << "iter " << iter << ": ни один инкремент total_voting_power не потерян";
        EXPECT_EQ(h->app->orgs->countActiveMembers(orgId), 1 + kLimit) << "iter " << iter;
    }
}

TEST(Concurrency, ConcurrentOrgMutationsLoseNoUpdates) {
    auto h = Harness::create();
    AgentContext admin = h->registerAgent("mix-admin");
    AgentContext a = h->registerAgent("mix-a");
    AgentContext b = h->registerAgent("mix-b");
    AgentContext c = h->registerAgent("mix-c");

    Json::Value orgOut = createOrg(*h, admin, "Mix Org", "OPEN",
                                   orgConfigArgs("MAJORITY", 3600, "SHARES", 51));
    ASSERT_FALSE(h->isError(orgOut));
    std::string orgId = orgOut["org_id"].asString();

    auto powerArgs = [&](const std::string& target, double v) {
        Json::Value p;
        p["org_id"] = orgId;
        p["target_agent_id"] = target;
        p["new_power"] = v;
        return p;
    };
    for (const auto* m : {&a, &b, &c}) {
        Json::Value joinArgs;
        joinArgs["org_id"] = orgId;
        ASSERT_FALSE(h->isError(h->call("join_organization", joinArgs, m)));
    }
    // Последовательная расстановка: admin=55, A=20, B=10, C=0 → total 85.
    ASSERT_FALSE(h->isError(h->call("update_voting_power", powerArgs(admin.agent_id, 55.0), &admin)));
    ASSERT_FALSE(h->isError(h->call("update_voting_power", powerArgs(a.agent_id, 20.0), &admin)));
    ASSERT_FALSE(h->isError(h->call("update_voting_power", powerArgs(b.agent_id, 10.0), &admin)));

    // Гонка: A→25, B→15, C выходит — операции коммутативны, потерь нет.
    std::barrier start(3);
    std::atomic<int> powerOk{0};
    std::vector<std::thread> threads;
    threads.emplace_back([&] {
        start.arrive_and_wait();
        if (!h->isError(h->call("update_voting_power", powerArgs(a.agent_id, 25.0), &admin))) powerOk.fetch_add(1);
    });
    threads.emplace_back([&] {
        start.arrive_and_wait();
        if (!h->isError(h->call("update_voting_power", powerArgs(b.agent_id, 15.0), &admin))) powerOk.fetch_add(1);
    });
    threads.emplace_back([&] {
        Json::Value lv;
        lv["org_id"] = orgId;
        start.arrive_and_wait();
        EXPECT_FALSE(h->isError(h->call("leave_organization", lv, &c)));
    });
    for (auto& t : threads) t.join();

    EXPECT_EQ(powerOk.load(), 2);
    auto org = h->app->orgs->get(orgId);
    ASSERT_TRUE(org.has_value());
    EXPECT_DOUBLE_EQ(org->total_voting_power, 95.0) << "потерянных обновлений нет";
    EXPECT_EQ(h->app->orgs->countActiveMembers(orgId), 3);

    double sum = 0.0;
    for (const auto* m : {&admin, &a, &b}) {
        if (auto mm = h->app->orgs->getMembership(orgId, m->agent_id)) sum += mm->voting_power;
    }
    EXPECT_DOUBLE_EQ(org->total_voting_power, sum);

    // Гонка с капой 100%: индивидуально допустимые смены вместе превышают лимит.
    Json::Value capOut = createOrg(*h, admin, "Mix Cap Org", "OPEN",
                                   orgConfigArgs("MAJORITY", 3600, "SHARES", 51));
    ASSERT_FALSE(h->isError(capOut));
    std::string capOrgId = capOut["org_id"].asString();
    for (const auto* m : {&a, &b}) {
        Json::Value joinArgs;
        joinArgs["org_id"] = capOrgId;
        ASSERT_FALSE(h->isError(h->call("join_organization", joinArgs, m)));
    }
    auto capPower = [&](const std::string& target, double v) {
        Json::Value p;
        p["org_id"] = capOrgId;
        p["target_agent_id"] = target;
        p["new_power"] = v;
        return p;
    };
    ASSERT_FALSE(h->isError(h->call("update_voting_power", capPower(admin.agent_id, 50.0), &admin)));

    std::barrier capStart(2);
    std::atomic<int> capOk{0};
    std::atomic<int> capRejected{0};
    std::vector<std::thread> capThreads;
    for (const auto* m : {&a, &b}) {
        capThreads.emplace_back([&, m] {
            capStart.arrive_and_wait();
            Json::Value r = h->call("update_voting_power", capPower(m->agent_id, 30.0), &admin);
            if (!h->isError(r)) {
                capOk.fetch_add(1);
            } else if (h->errorCode(r) == -32005) {
                capRejected.fetch_add(1);
            }
        });
    }
    for (auto& t : capThreads) t.join();

    EXPECT_EQ(capOk.load(), 1) << "ровно одна смена проходит";
    EXPECT_EQ(capRejected.load(), 1) << "вторая отклонена -32005";
    auto capOrg = h->app->orgs->get(capOrgId);
    ASSERT_TRUE(capOrg.has_value());
    EXPECT_LE(capOrg->total_voting_power, 100.0 + kEps) << "капа 100% не нарушена";
    double capSum = 0.0;
    for (const auto* m : {&admin, &a, &b}) {
        if (auto mm = h->app->orgs->getMembership(capOrgId, m->agent_id)) capSum += mm->voting_power;
    }
    EXPECT_DOUBLE_EQ(capOrg->total_voting_power, capSum);
}

TEST(Concurrency, VoteRacesDissolveExactlyOneOutcome) {
    constexpr int kIterations = 30;
    for (int iter = 0; iter < kIterations; ++iter) {
        auto h = Harness::create();
        AgentContext creator = h->registerAgent("vd-creator");
        AgentContext voterA = h->registerAgent("vd-a");
        AgentContext voterB = h->registerAgent("vd-b");
        Json::Value orgOut = createOrg(*h, creator, "VD Org " + std::to_string(iter), "OPEN",
                                       orgConfigArgs("QUORUM_PERCENTAGE", 3600, "EQUAL", 33));
        ASSERT_FALSE(h->isError(orgOut)) << "iter " << iter;
        std::string orgId = orgOut["org_id"].asString();
        for (const auto* ag : {&voterA, &voterB}) {
            Json::Value joinArgs;
            joinArgs["org_id"] = orgId;
            ASSERT_FALSE(h->isError(h->call("join_organization", joinArgs, ag)));
        }
        // Один YES набирает кворум 33% при T=3 → голос может закрыть PASSED
        // и применить config_delta — гонка эффектов с роспуском воспроизводима.
        Json::Value pArgs;
        pArgs["org_id"] = orgId;
        pArgs["title"] = "vd-" + std::to_string(iter);
        Json::Value delta;
        delta["quorum_percentage"] = 66;
        pArgs["config_delta"] = delta;
        Json::Value pOut = h->call("create_proposal", pArgs, &creator);
        ASSERT_FALSE(h->isError(pOut)) << "iter " << iter;
        std::string pid = pOut["proposal_id"].asString();

        std::barrier start(2);
        std::atomic<int> voteOutcome{0};  // 1 ok, 2 -32003, 3 -32004
        std::thread voter([&] {
            start.arrive_and_wait();
            auto r = h->app->engine->castVote(voterA.agent_id, pid, VoteDecision::YES);
            if (r.ok()) voteOutcome.store(1);
            else if (r.error().code == -32003) voteOutcome.store(2);
            else if (r.error().code == -32004) voteOutcome.store(3);
        });
        std::thread dissolver([&] {
            start.arrive_and_wait();
            auto res = h->app->engine->dissolveOrganization(orgId, creator.agent_id);
            EXPECT_TRUE(res.ok()) << "iter " << iter;
        });
        voter.join();
        dissolver.join();

        EXPECT_NE(voteOutcome.load(), 0) << "iter " << iter;

        auto p = h->app->proposals->get(orgId, pid);
        ASSERT_TRUE(p.has_value()) << "iter " << iter;
        EXPECT_NE(p->status, ProposalStatus::ACTIVE) << "iter " << iter << ": resurrection невозможна";

        double stored = 0;
        int rows = 0;
        for (const auto* ag : {&creator, &voterA, &voterB}) {
            if (auto v = h->app->votes->get(orgId, pid, ag->agent_id)) {
                ++rows;
                stored += v->power_at_vote;
            }
        }
        EXPECT_DOUBLE_EQ(stored, p->yes_power + p->no_power + p->abstain_power)
            << "iter " << iter << ": счётчики равны сумме зафиксированных голосов";
        EXPECT_EQ(rows, p->voters_count) << "iter " << iter;
        if (voteOutcome.load() == 1) EXPECT_EQ(rows, 1) << "iter " << iter;
        else EXPECT_EQ(rows, 0) << "iter " << iter << ": отклонённый голос не зафиксирован";

        for (const auto* ag : {&voterB, &voterA}) {
            auto late = h->app->engine->castVote(ag->agent_id, pid, VoteDecision::YES);
            ASSERT_FALSE(late.ok()) << "iter " << iter;
            EXPECT_TRUE(late.error().code == -32003 || late.error().code == -32004)
                << "iter " << iter << ": поздний голос отклонён";
        }

        auto org = h->app->orgs->get(orgId);
        ASSERT_TRUE(org.has_value()) << "iter " << iter;
        ASSERT_EQ(org->status, OrgStatus::DISSOLVED) << "iter " << iter;
        const bool deltaApplied = org->config.quorum_percentage == 66;
        if (deltaApplied) {
            EXPECT_EQ(p->status, ProposalStatus::PASSED) << "iter " << iter
                << ": дельта применена только при полном PASSED до роспуска";
            EXPECT_EQ(rows, 1) << "iter " << iter;
        } else {
            EXPECT_EQ(org->config.quorum_percentage, 33) << "iter " << iter
                << ": эффекты PASSED не применяются к распущенной организации";
        }
        for (const auto& pr : h->app->proposals->listByOrg(orgId)) {
            EXPECT_NE(pr.status, ProposalStatus::ACTIVE) << "iter " << iter << ": живых предложений нет";
        }
    }
}

TEST(Concurrency, CreateProposalRacesDissolve) {
    constexpr int kIterations = 10;
    for (int iter = 0; iter < kIterations; ++iter) {
        auto h = Harness::create();
        AgentContext creator = h->registerAgent("cd-creator");
        AgentContext member = h->registerAgent("cd-member");
        Json::Value orgOut = createOrg(*h, creator, "CD Org " + std::to_string(iter), "OPEN",
                                       orgConfigArgs("MAJORITY", 3600));
        ASSERT_FALSE(h->isError(orgOut)) << "iter " << iter;
        std::string orgId = orgOut["org_id"].asString();
        Json::Value joinArgs;
        joinArgs["org_id"] = orgId;
        ASSERT_FALSE(h->isError(h->call("join_organization", joinArgs, &member)));

        std::atomic<bool> dissolved{false};
        std::mutex idsMutex;
        std::vector<std::string> createdIds;
        std::thread proposer([&] {
            for (int i = 0; i < 200 && !dissolved.load(); ++i) {
                Json::Value r = createProposal(*h, member, orgId, "cd-" + std::to_string(i));
                if (h->isError(r)) {
                    EXPECT_EQ(h->errorCode(r), -32004) << "iter " << iter;
                    break;
                }
                std::lock_guard lk(idsMutex);
                createdIds.push_back(r["proposal_id"].asString());
            }
        });
        std::thread dissolver([&] {
            auto res = h->app->engine->dissolveOrganization(orgId, creator.agent_id);
            EXPECT_TRUE(res.ok()) << "iter " << iter;
            dissolved.store(true);
        });
        proposer.join();
        dissolver.join();

        h->clock.advanceSeconds(7200);
        h->app->engine->closeExpired(h->clock.nowSec());

        auto org = h->app->orgs->get(orgId);
        ASSERT_TRUE(org.has_value()) << "iter " << iter;
        ASSERT_EQ(org->status, OrgStatus::DISSOLVED) << "iter " << iter;
        for (const auto& pr : h->app->proposals->listByOrg(orgId)) {
            EXPECT_EQ(pr.status, ProposalStatus::EXPIRED)
                << "iter " << iter << ": в распущенной организации нет живых предложений";
        }
        for (const auto& pid : createdIds) {
            auto p = h->app->proposals->get(orgId, pid);
            ASSERT_TRUE(p.has_value()) << "iter " << iter;
            EXPECT_EQ(p->status, ProposalStatus::EXPIRED) << "iter " << iter;
        }
    }
}
