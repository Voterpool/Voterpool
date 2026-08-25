#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

using namespace voterpool;
using namespace voterpool::testing;

TEST(StorageRepos, OpensAllColumnFamilies) {
    auto h = Harness::create();
    for (const char* name : RocksDBWrapper::kCfNames) {
        EXPECT_NE(h->app->db->cf(name), nullptr) << name;
    }
}

TEST(StorageRepos, AgentRoundTripPreservesFields) {
    auto h = Harness::create();
    AgentContext a = h->registerAgent("Round Trip");
    auto agent = h->app->agents->get(a.agent_id);
    ASSERT_TRUE(agent.has_value());
    EXPECT_EQ(agent->name, "Round Trip");
    EXPECT_FALSE(agent->api_key_hash.empty());
    EXPECT_GT(agent->created_at, 0);
    EXPECT_GT(agent->updated_at, 0);
}

TEST(StorageRepos, OrgAndMembershipRoundTrip) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("org-creator");
    Json::Value orgOut = createOrg(*h, creator, "RT Org", "CLOSED", orgConfigArgs("MAJORITY", 300, "SHARES"));
    ASSERT_FALSE(h->isError(orgOut));
    std::string orgId = orgOut["org_id"].asString();

    auto org = h->app->orgs->get(orgId);
    ASSERT_TRUE(org.has_value());
    EXPECT_EQ(org->name, "RT Org");
    EXPECT_DOUBLE_EQ(org->total_voting_power, 100.0);
    EXPECT_EQ(org->config.power_distribution, PowerDistribution::SHARES);
    EXPECT_EQ(toString(org->type), std::string("CLOSED"));

    auto m = h->app->orgs->getMembership(orgId, creator.agent_id);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->role, MemberRole::ADMIN);
    EXPECT_DOUBLE_EQ(m->voting_power, 100.0);

    auto orgsOfCreator = h->app->orgs->listOrgsOfAgent(creator.agent_id);
    ASSERT_EQ(orgsOfCreator.size(), 1u);
    EXPECT_EQ(orgsOfCreator[0].org_id, orgId);
}

TEST(StorageRepos, ReverseIndexTracksPendingActiveAndRemoval) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("rev-idx-admin");
    AgentContext member = h->registerAgent("rev-idx-member");
    Json::Value orgOut = createOrg(*h, creator, "Reverse Org", "CLOSED", orgConfigArgs("MAJORITY", 300));
    ASSERT_FALSE(h->isError(orgOut));
    std::string orgId = orgOut["org_id"].asString();

    Membership m;
    m.org_id = orgId;
    m.agent_id = member.agent_id;
    m.role = MemberRole::MEMBER;
    m.voting_power = 1.0;
    m.status = MemberStatus::PENDING;
    m.created_at = h->clock.nowSec();
    m.updated_at = m.created_at;

    // PENDING сразу виден в обратном индексе (agent-identity: профиль с членствами).
    ASSERT_TRUE(h->app->orgs->putMembership(m));
    auto listed = h->app->orgs->listOrgsOfAgent(member.agent_id);
    ASSERT_EQ(listed.size(), 1u);
    EXPECT_EQ(listed[0].org_id, orgId);
    EXPECT_EQ(listed[0].status, MemberStatus::PENDING);

    // Переход PENDING -> ACTIVE не создаёт дубль.
    m.status = MemberStatus::ACTIVE;
    m.updated_at = h->clock.nowSec();
    ASSERT_TRUE(h->app->orgs->putMembership(m));
    listed = h->app->orgs->listOrgsOfAgent(member.agent_id);
    ASSERT_EQ(listed.size(), 1u);
    EXPECT_EQ(listed[0].status, MemberStatus::ACTIVE);

    // Удаление членства чистит обе записи (cf_memberships и cf_agent_orgs).
    ASSERT_TRUE(h->app->orgs->deleteMembership(m));
    EXPECT_TRUE(h->app->orgs->listOrgsOfAgent(member.agent_id).empty());
    EXPECT_FALSE(h->app->orgs->getMembership(orgId, member.agent_id).has_value());
}

TEST(StorageRepos, BatchOrgPutVisibleOnlyAfterCommit) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("batch-org");
    Json::Value orgOut = createOrg(*h, creator, "Batch Org", "OPEN", orgConfigArgs("MAJORITY", 300));
    ASSERT_FALSE(h->isError(orgOut));
    std::string orgId = orgOut["org_id"].asString();

    auto org = h->app->orgs->get(orgId);
    ASSERT_TRUE(org.has_value());
    org->name = "Renamed In Batch";

    rocksdb::WriteBatch batch;
    h->app->orgs->put(batch, *org);
    auto before = h->app->orgs->get(orgId);
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(before->name, "Batch Org") << "до коммита батч невидим читателям";

    ASSERT_TRUE(h->app->db->commit(batch));
    auto after = h->app->orgs->get(orgId);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->name, "Renamed In Batch");
}

TEST(StorageRepos, ProposalAndVoteRoundTripWithAggregates) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("prop-creator");
    AgentContext voter = h->registerAgent("prop-voter");
    Json::Value orgOut = createOrg(*h, creator, "Prop Org", "OPEN",
                                orgConfigArgs("QUORUM_PERCENTAGE", 600, "EQUAL", 100));
    std::string orgId = orgOut["org_id"].asString();
    h->call("join_organization", [&] { Json::Value a; a["org_id"] = orgId; return a; }(), &voter);
    Json::Value pOut = createProposal(*h, creator, orgId, "Ship it");
    std::string pid = pOut["proposal_id"].asString();
    vote(*h, voter, pid, "YES");

    auto p = h->app->proposals->get(orgId, pid);
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->title, "Ship it");
    EXPECT_EQ(p->status, ProposalStatus::ACTIVE);
    EXPECT_DOUBLE_EQ(p->yes_power, 1.0);
    EXPECT_EQ(p->voters_count, 1);
    EXPECT_DOUBLE_EQ(p->total_voting_power_at_creation, 2.0);
    EXPECT_EQ(p->expires_at, p->created_at + 600);

    auto v = h->app->votes->get(orgId, pid, voter.agent_id);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(v->decision, VoteDecision::YES);
    EXPECT_DOUBLE_EQ(v->power_at_vote, 1.0);
}
