#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

using namespace voterpool;
using namespace voterpool::testing;

TEST(CastVoteTx, VoteAndCountersAreAtomic) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("tx-a");
    AgentContext voter = h->registerAgent("tx-b");
    AgentContext third = h->registerAgent("tx-c");
    Json::Value orgOut = createOrg(*h, creator, "Tx Org", "OPEN", orgConfigArgs("MAJORITY", 600));
    std::string orgId = orgOut["org_id"].asString();
    for (auto* a : {&voter, &third}) {
        h->call("join_organization",
                [&] { Json::Value x; x["org_id"] = orgId; return x; }(), a);
    }
    Json::Value pOut = createProposal(*h, creator, orgId, "atomic");
    std::string pid = pOut["proposal_id"].asString();

    vote(*h, creator, pid, "YES");
    vote(*h, voter, pid, "NO");

    auto p = h->app->proposals->get(orgId, pid);
    ASSERT_TRUE(p.has_value());
    EXPECT_DOUBLE_EQ(p->yes_power, 1.0);
    EXPECT_DOUBLE_EQ(p->no_power, 1.0);
    EXPECT_EQ(p->voters_count, 2);
    EXPECT_EQ(p->status, ProposalStatus::ACTIVE);

    double sum = p->yes_power + p->no_power + p->abstain_power;
    double votes = 0;
    for (const auto& agentId : {creator.agent_id, voter.agent_id}) {
        auto v = h->app->votes->get(orgId, pid, agentId);
        ASSERT_TRUE(v.has_value());
        votes += v->power_at_vote;
    }
    EXPECT_DOUBLE_EQ(sum, votes);
}

TEST(CastVoteTx, FailedCommitLeavesNoPartialState) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("fail-a");
    Json::Value orgOut = createOrg(*h, creator, "Fail Org", "OPEN", orgConfigArgs("MAJORITY", 600));
    std::string orgId = orgOut["org_id"].asString();
    Json::Value pOut = createProposal(*h, creator, orgId, "crash");
    std::string pid = pOut["proposal_id"].asString();

    h->app->db->beforeCommitHook = [] { return false; };
    Json::Value out = vote(*h, creator, pid, "YES");
    h->app->db->beforeCommitHook = nullptr;
    EXPECT_TRUE(h->isError(out));
    EXPECT_FALSE(DbHealth::instance().healthy());
    DbHealth::instance().reset();

    EXPECT_FALSE(h->app->votes->get(orgId, pid, creator.agent_id).has_value());
    auto p = h->app->proposals->get(orgId, pid);
    ASSERT_TRUE(p.has_value());
    EXPECT_DOUBLE_EQ(p->yes_power, 0.0);
    EXPECT_EQ(p->voters_count, 0);
    EXPECT_EQ(p->status, ProposalStatus::ACTIVE);
}

TEST(CastVoteTx, DuplicateAndClosedVotesRejected) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("dup-a");
    AgentContext voter = h->registerAgent("dup-b");
    Json::Value orgOut = createOrg(*h, creator, "Dup Org", "OPEN", orgConfigArgs("MAJORITY", 600));
    std::string orgId = orgOut["org_id"].asString();
    h->call("join_organization", [&] { Json::Value a; a["org_id"] = orgId; return a; }(), &voter);
    Json::Value pOut = createProposal(*h, creator, orgId, "dup");
    std::string pid = pOut["proposal_id"].asString();

    Json::Value first = vote(*h, voter, pid, "YES");
    ASSERT_FALSE(h->isError(first));
    Json::Value second = vote(*h, voter, pid, "NO");
    EXPECT_EQ(h->errorCode(second), -32003);
    EXPECT_EQ(second["previous_decision"].asString(), "YES");

    vote(*h, creator, pid, "YES");
    vote(*h, creator, pid, "YES");
    Json::Value closed = vote(*h, voter, pid, "NO");
    EXPECT_EQ(h->errorCode(closed), -32003);
    EXPECT_EQ(closed["current_status"].asString(), "PASSED");
}

TEST(CastVoteTx, DecisionValidatedAgainstModel) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("model-a");
    Json::Value orgOut = createOrg(*h, creator, "Model Org", "OPEN",
                                   orgConfigArgs("QUORUM_PERCENTAGE", 600, "EQUAL", 51));
    std::string orgId = orgOut["org_id"].asString();
    Json::Value pOut = createProposal(*h, creator, orgId, "model");
    std::string pid = pOut["proposal_id"].asString();

    Json::Value abstain = vote(*h, creator, pid, "ABSTAIN");
    EXPECT_EQ(h->errorCode(abstain), -32005);
    EXPECT_EQ(abstain["consensus_model"].asString(), "QUORUM_PERCENTAGE");
}
