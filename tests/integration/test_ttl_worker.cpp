#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

using namespace voterpool;
using namespace voterpool::testing;

TEST(TtlWorker, ClosesExpiredWithModelSpecificOutcomes) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("ttl-admin");

    Json::Value quorumOrg = createOrg(*h, creator, "Ttl Quorum", "OPEN",
                                      orgConfigArgs("QUORUM_PERCENTAGE", 2, "EQUAL", 100));
    Json::Value majorityOrg = createOrg(*h, creator, "Ttl Majority", "OPEN",
                                        orgConfigArgs("MAJORITY", 2));
    Json::Value consentOrg = createOrg(*h, creator, "Ttl Consent", "OPEN",
                                       orgConfigArgs("CONSENT", 2));
    std::string quorumId = quorumOrg["org_id"].asString();
    std::string majorityId = majorityOrg["org_id"].asString();
    std::string consentId = consentOrg["org_id"].asString();

    std::string quorumP = createProposal(*h, creator, quorumId, "q")["proposal_id"].asString();
    std::string majorityP = createProposal(*h, creator, majorityId, "m")["proposal_id"].asString();
    std::string consentP = createProposal(*h, creator, consentId, "c")["proposal_id"].asString();

    vote(*h, creator, consentP, "ABSTAIN");

    h->clock.advanceSeconds(3);
    h->app->engine->closeExpired(h->clock.nowSec());

    auto qAfter = h->app->proposals->get(quorumId, quorumP);
    EXPECT_EQ(qAfter->status, ProposalStatus::EXPIRED) << "quorum not met -> EXPIRED";
    auto mAfter = h->app->proposals->get(majorityId, majorityP);
    EXPECT_EQ(mAfter->status, ProposalStatus::REJECTED) << "majority: never EXPIRED";
    auto cAfter = h->app->proposals->get(consentId, consentP);
    EXPECT_EQ(cAfter->status, ProposalStatus::EXPIRED) << "consent: abstain-only -> EXPIRED";
}

TEST(TtlWorker, ExpiredRejectedProposalLeavesOrgUntouched) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("ttl-delta");

    Json::Value orgOut = createOrg(*h, creator, "Ttl Delta", "OPEN",
                                   orgConfigArgs("MAJORITY", 1, "EQUAL", 51));
    std::string orgId = orgOut["org_id"].asString();

    Json::Value pArgs;
    pArgs["org_id"] = orgId;
    pArgs["title"] = "switch to CONSENT";
    pArgs["config_delta"] = orgConfigArgs("CONSENT", 5000);
    Json::Value pOut = h->call("create_proposal", pArgs, &creator);
    std::string pid = pOut["proposal_id"].asString();
    ASSERT_FALSE(h->isError(pOut));

    h->clock.advanceSeconds(2);
    h->app->engine->closeExpired(h->clock.nowSec());

    auto after = h->app->proposals->get(orgId, pid);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->status, ProposalStatus::REJECTED)
        << "MAJORITY without votes expires as REJECTED (EXPIRED impossible)";
    auto org = h->app->orgs->get(orgId);
    ASSERT_TRUE(org.has_value());
    EXPECT_EQ(org->config.consensus_model, ConsensusModel::MAJORITY)
        << "config_delta of a rejected proposal must not be applied";

    bool configAudited = false;
    for (const auto& e : h->app->audit->listByOrg(orgId)) {
        if (e.action == "CONFIG_CHANGED" && e.proposal_id == pid) configAudited = true;
    }
    EXPECT_FALSE(configAudited);
}

TEST(TtlWorker, ActiveIndexCleanedAfterClosure) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("ttl-index");
    Json::Value orgOut = createOrg(*h, creator, "Ttl Index", "OPEN", orgConfigArgs("MAJORITY", 1));
    std::string orgId = orgOut["org_id"].asString();
    std::string pid = createProposal(*h, creator, orgId, "idx")["proposal_id"].asString();

    h->clock.advanceSeconds(2);
    h->app->engine->closeExpired(h->clock.nowSec());

    auto it = h->app->db->newIterator("cf_indexes");
    size_t remaining = 0;
    for (it->Seek("active_proposals:"); it->Valid(); it->Next()) {
        if (it->key().ToString().rfind("active_proposals:", 0) != 0) break;
        ++remaining;
    }
    EXPECT_EQ(remaining, 0u);
}
