#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

using namespace voterpool;
using namespace voterpool::testing;

namespace {

Json::Value orgIdArgs(const std::string& orgId) {
    Json::Value a;
    a["org_id"] = orgId;
    return a;
}

// OPEN-организация EQUAL+CONSENT. Первые `preJoined` агентов вступают ДО
// создания предложений (попадают в замороженный H).
struct ConsentOrg {
    std::shared_ptr<Harness> h;
    AgentContext creator;
    AgentContext m1;
    AgentContext m2;
    std::string orgId;

    static ConsentOrg make(const std::string& name, int preJoined) {
        ConsentOrg c;
        c.h = Harness::create();
        c.creator = c.h->registerAgent(name + "-admin");
        Json::Value out = createOrg(*c.h, c.creator, name, "OPEN", orgConfigArgs("CONSENT", 600));
        c.orgId = out["org_id"].asString();
        c.m1 = c.h->registerAgent(name + "-m1");
        if (preJoined >= 1) c.h->call("join_organization", orgIdArgs(c.orgId), &c.m1);
        if (preJoined >= 2) {
            c.m2 = c.h->registerAgent(name + "-m2");
            c.h->call("join_organization", orgIdArgs(c.orgId), &c.m2);
        }
        return c;
    }

    std::string newProposal(const std::string& title) {
        return createProposal(*h, creator, orgId, title)["proposal_id"].asString();
    }
};

}  // namespace

TEST(ConsentFlow, FullCircleWithAbstainPassesOnLastVote) {
    ConsentOrg c = ConsentOrg::make("Circle", 2);
    std::string pid = c.newProposal("full circle");

    Json::Value out1 = vote(*c.h, c.creator, pid, "YES");
    EXPECT_EQ(out1["proposal_status"].asString(), "ACTIVE") << "первый «за» не закрывает предложение";

    Json::Value out2 = vote(*c.h, c.m1, pid, "ABSTAIN");
    EXPECT_EQ(out2["proposal_status"].asString(), "ACTIVE") << "круг ещё не завершён";

    auto listed = c.h->call("get_proposals",
                            [&] {
                                Json::Value a = orgIdArgs(c.orgId);
                                a["filter"] = "ACTIVE";
                                return a;
                            }(),
                            &c.creator);
    ASSERT_EQ(listed.size(), 1u);
    EXPECT_EQ(listed[0]["voters_count"].asInt64(), 2);
    EXPECT_EQ(listed[0]["eligible_voters_at_creation"].asInt64(), 3);

    Json::Value out3 = vote(*c.h, c.m2, pid, "ABSTAIN");
    EXPECT_EQ(out3["proposal_status"].asString(), "PASSED") << "круг замкнулся без возражений";

    EXPECT_EQ(c.h->errorCode(vote(*c.h, c.creator, pid, "YES")), -32003)
        << "повторное голосование запрещено";
}

TEST(ConsentFlow, AnyNoRejectsBeforeDeadline) {
    ConsentOrg c = ConsentOrg::make("Objection", 1);
    std::string pid = c.newProposal("objection round");
    EXPECT_EQ(c.h->errorCode(vote(*c.h, c.creator, pid, "YES")), 0);

    Json::Value out = vote(*c.h, c.m1, pid, "NO");
    EXPECT_EQ(out["proposal_status"].asString(), "REJECTED");

    auto p = c.h->app->proposals->get(c.orgId, pid);
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->status, ProposalStatus::REJECTED);
}

TEST(ConsentFlow, TtlWithPartialTurnoutExpiresDespiteYes) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("ttl-consent-admin");
    Json::Value orgOut =
        createOrg(*h, creator, "Ttl Consent Flow", "OPEN", orgConfigArgs("CONSENT", 2));
    std::string orgId = orgOut["org_id"].asString();
    AgentContext m1 = h->registerAgent("ttl-consent-m1");
    h->call("join_organization", orgIdArgs(orgId), &m1);
    std::string pid = createProposal(*h, creator, orgId, "partial turnout")["proposal_id"].asString();

    EXPECT_EQ(h->errorCode(vote(*h, creator, pid, "YES")), 0);

    h->clock.advanceSeconds(3);
    h->app->engine->closeExpired(h->clock.nowSec());

    auto p = h->app->proposals->get(orgId, pid);
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->status, ProposalStatus::EXPIRED)
        << "неявка не трактуется как согласие: круг не завершён -> EXPIRED";
}

TEST(ConsentFlow, ConsentSharesRejectedOnCreateOrgAndDelta) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("shares-admin");

    Json::Value badArgs;
    badArgs["name"] = "Consent Shares Org";
    badArgs["type"] = "OPEN";
    badArgs["config"] = orgConfigArgs("CONSENT", 600, "SHARES");
    Json::Value out = h->call("create_organization", badArgs, &creator);
    EXPECT_EQ(h->errorCode(out), -32005) << "CONSENT требует EQUAL при создании организации";

    Json::Value orgOut = createOrg(*h, creator, "Delta Target Org", "OPEN",
                                   orgConfigArgs("MAJORITY", 600, "SHARES"));
    std::string orgId = orgOut["org_id"].asString();

    Json::Value pArgs;
    pArgs["org_id"] = orgId;
    pArgs["title"] = "switch to CONSENT";
    pArgs["config_delta"] = orgConfigArgs("CONSENT", 5000, "SHARES");
    Json::Value deltaOut = h->call("create_proposal", pArgs, &creator);
    EXPECT_EQ(h->errorCode(deltaOut), -32005) << "config_delta не может сочетать CONSENT и SHARES";

    Json::Value distOnly;
    distOnly["org_id"] = orgId;
    distOnly["title"] = "switch model only";
    distOnly["config_delta"] = orgConfigArgs("MAJORITY", 5000, "EQUAL");
    EXPECT_EQ(h->errorCode(h->call("create_proposal", distOnly, &creator)), 0)
        << "согласованная дельта проходит";
}

TEST(ConsentFlow, LateJoinerVoteCountsTowardFrozenHeadCount) {
    ConsentOrg c = ConsentOrg::make("LateJoiner", 1);
    std::string pid = c.newProposal("late joiner");

    AgentContext m2 = c.h->registerAgent("latejoiner-m2");
    c.h->call("join_organization", orgIdArgs(c.orgId), &m2);

    EXPECT_EQ(c.h->errorCode(vote(*c.h, m2, pid, "ABSTAIN")), 0);
    auto active = c.h->call("get_proposals",
                            [&] {
                                Json::Value a = orgIdArgs(c.orgId);
                                a["filter"] = "ACTIVE";
                                return a;
                            }(),
                            &c.creator);
    ASSERT_EQ(active.size(), 1u);
    EXPECT_EQ(active[0]["voters_count"].asInt64(), 1) << "голос новичка принят";
    EXPECT_EQ(active[0]["eligible_voters_at_creation"].asInt64(), 2)
        << "H заморожен на момент создания";

    Json::Value out = vote(*c.h, c.creator, pid, "YES");
    EXPECT_EQ(out["proposal_status"].asString(), "PASSED")
        << "упрощение voters_count >= H задокументировано: голоса вступивших после создания "
           "учитываются в voters_count";
}
