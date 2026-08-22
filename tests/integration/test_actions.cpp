#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

using namespace voterpool;
using namespace voterpool::testing;

namespace {

struct ClosedOrg {
    std::shared_ptr<Harness> h;
    AgentContext creator;
    AgentContext candidate;
    std::string orgId;
};

ClosedOrg makeClosedOrg(const std::string& name, const char* model = "CONSENT") {
    ClosedOrg c;
    c.h = Harness::create();
    c.creator = c.h->registerAgent(name + "-admin");
    Json::Value args;
    args["name"] = name + " Org";
    args["type"] = "CLOSED";
    args["config"] = orgConfigArgs(model, 600);
    Json::Value out = c.h->call("create_organization", args, &c.creator);
    c.orgId = out["org_id"].asString();
    c.candidate = c.h->registerAgent(name + "-candidate");
    Json::Value joinArgs;
    joinArgs["org_id"] = c.orgId;
    auto joined = c.h->call("join_organization", joinArgs, &c.candidate);
    EXPECT_EQ(joined["status"].asString(), "PENDING");
    return c;
}

Json::Value approveProposal(ClosedOrg& c) {
    Json::Value args;
    args["org_id"] = c.orgId;
    args["title"] = "Approve " + c.candidate.agent_id;
    Json::Value action;
    action["kind"] = "APPROVE_MEMBER";
    Json::Value payload;
    payload["target_agent_id"] = c.candidate.agent_id;
    action["payload"] = payload;
    args["action"] = action;
    return c.h->call("create_proposal", args, &c.creator);
}

}  // namespace

TEST(Actions, ApproveMemberActivatesPendingViaConsensus) {
    ClosedOrg c = makeClosedOrg("Approve");
    Json::Value pOut = approveProposal(c);
    ASSERT_FALSE(c.h->isError(pOut));
    std::string pid = pOut["proposal_id"].asString();

    EXPECT_EQ(c.h->errorCode(vote(*c.h, c.creator, pid, "YES")), 0);

    auto m = c.h->app->orgs->getMembership(c.orgId, c.candidate.agent_id);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->status, MemberStatus::ACTIVE);
    EXPECT_FALSE(c.h->app->indexes->hasPending(c.orgId, c.candidate.agent_id));
    EXPECT_EQ(c.h->app->indexes->getJoinLimit(c.orgId, 1700000000), 1);

    auto org = c.h->app->orgs->get(c.orgId);
    EXPECT_DOUBLE_EQ(org->total_voting_power, 2.0);

    bool foundInReverseIndex = false;
    for (const auto& om : c.h->app->orgs->listOrgsOfAgent(c.candidate.agent_id)) {
        if (om.org_id == c.orgId && om.status == MemberStatus::ACTIVE) foundInReverseIndex = true;
    }
    EXPECT_TRUE(foundInReverseIndex);
}

TEST(Actions, ApproveMemberIsIdempotentOnReactivation) {
    ClosedOrg c = makeClosedOrg("Idem");
    Json::Value first = approveProposal(c);
    std::string pid1 = first["proposal_id"].asString();
    vote(*c.h, c.creator, pid1, "YES");

    Json::Value second = approveProposal(c);
    std::string pid2 = second["proposal_id"].asString();
    vote(*c.h, c.creator, pid2, "YES");

    auto org = c.h->app->orgs->get(c.orgId);
    EXPECT_DOUBLE_EQ(org->total_voting_power, 2.0);
    EXPECT_EQ(c.h->app->indexes->getJoinLimit(c.orgId, 1700000000), 1);
}

TEST(Actions, ApproveMemberSkippedWhenOrgFull) {
    ClosedOrg c = makeClosedOrg("Full");
    auto orgBefore = c.h->app->orgs->get(c.orgId);
    Organization limited = *orgBefore;
    limited.max_agents = 1;
    c.h->app->orgs->put(limited);

    Json::Value pOut = approveProposal(c);
    std::string pid = pOut["proposal_id"].asString();
    vote(*c.h, c.creator, pid, "YES");

    auto p = c.h->app->proposals->get(c.orgId, pid);
    EXPECT_EQ(p->status, ProposalStatus::PASSED);
    auto m = c.h->app->orgs->getMembership(c.orgId, c.candidate.agent_id);
    EXPECT_EQ(m->status, MemberStatus::PENDING);
    EXPECT_EQ(c.h->app->indexes->getJoinLimit(c.orgId, 1700000000), 0);
}

TEST(Actions, UpdateOrgInfoAppliesAndReindexesAtomically) {
    ClosedOrg c = makeClosedOrg("Meta");
    Json::Value args;
    args["org_id"] = c.orgId;
    args["title"] = "Update profile";
    Json::Value action;
    action["kind"] = "UPDATE_ORG_INFO";
    Json::Value payload;
    payload["name"] = "Renamed Org";
    payload["category"] = "governance";
    Json::Value tags(Json::arrayValue);
    tags.append("gov");
    payload["tags"] = tags;
    payload["max_agents"] = 42;
    action["payload"] = payload;
    args["action"] = action;
    Json::Value pOut = c.h->call("create_proposal", args, &c.creator);
    std::string pid = pOut["proposal_id"].asString();
    vote(*c.h, c.creator, pid, "YES");

    AgentContext viewer = c.h->registerAgent("meta-viewer");
    Json::Value prof = getOrg(*c.h, c.orgId, viewer);
    ASSERT_TRUE(prof.isObject());
    EXPECT_EQ(prof["name"].asString(), "Renamed Org");
    EXPECT_EQ(prof["category"].asString(), "governance");
    EXPECT_EQ(prof["max_agents"].asInt64(), 42);

    Json::Value searchArgs;
    searchArgs["query"] = "renamed";
    Json::Value found = c.h->call("search_organizations", searchArgs, &viewer);
    EXPECT_EQ(found["items"].size(), 1u);
    searchArgs["query"] = "meta";
    found = c.h->call("search_organizations", searchArgs, &viewer);
    EXPECT_EQ(found["items"].size(), 0u);

    searchArgs.clear();
    searchArgs["category"] = "governance";
    found = c.h->call("search_organizations", searchArgs, &viewer);
    EXPECT_EQ(found["items"].size(), 1u);

    bool audited = false;
    for (const auto& e : c.h->app->audit->listByOrg(c.orgId)) {
        if (e.action == "ORG_INFO_UPDATED" || e.action == "CONFIG_CHANGED") audited = true;
    }
    EXPECT_TRUE(audited);
}
