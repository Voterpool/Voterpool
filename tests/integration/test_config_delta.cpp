#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

using namespace voterpool;
using namespace voterpool::testing;

namespace {

Json::Value deltaProposal(Harness& h, const AgentContext& author, const std::string& orgId,
                          const Json::Value& delta, const std::string& title = "config change") {
    Json::Value args;
    args["org_id"] = orgId;
    args["title"] = title;
    args["config_delta"] = delta;
    return h.call("create_proposal", args, &author);
}

}  // namespace

// Спека organizations «Частичная дельта наследует действующую конфигурацию»:
// отсутствующие поля не сбрасываются к значениям по умолчанию.
TEST(ConfigDelta, PartialDurationDeltaPreservesModelQuorumAndDistribution) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("delta-admin");
    Json::Value orgOut = createOrg(*h, creator, "Delta Consent Org", "OPEN",
                                   orgConfigArgs("CONSENT", 3600));
    ASSERT_FALSE(h->isError(orgOut));
    std::string orgId = orgOut["org_id"].asString();

    Json::Value delta;
    delta["voting_duration_sec"] = 7200;
    Json::Value pOut = deltaProposal(*h, creator, orgId, delta, "extend duration");
    ASSERT_FALSE(h->isError(pOut)) << pOut.toStyledString();
    std::string pid = pOut["proposal_id"].asString();

    Json::Value receipt = vote(*h, creator, pid, "YES");
    ASSERT_EQ(receipt["proposal_status"].asString(), "PASSED");

    AgentContext viewer = h->registerAgent("delta-viewer");
    Json::Value prof = getOrg(*h, orgId, viewer);
    const Json::Value& cfg = prof["config"];
    EXPECT_EQ(cfg["consensus_model"].asString(), "CONSENT") << "модель не должна сбрасываться в MAJORITY";
    EXPECT_EQ(cfg["power_distribution"].asString(), "EQUAL");
    EXPECT_EQ(cfg["quorum_percentage"].asInt(), 51);
    EXPECT_EQ(cfg["voting_duration_sec"].asInt64(), 7200);

    // mcp-protocol: карточка показывает полный смерженный конфиг и признак применения.
    Json::Value cardArgs;
    cardArgs["proposal_id"] = pid;
    Json::Value card = h->call("get_proposal", cardArgs, &creator);
    ASSERT_FALSE(h->isError(card));
    EXPECT_EQ(card["config_delta_applied"].asBool(), true);
    EXPECT_EQ(card["config_delta"]["voting_duration_sec"].asInt64(), 7200);
    EXPECT_EQ(card["config_delta"]["consensus_model"].asString(), "CONSENT");
    EXPECT_EQ(card["config_delta"]["quorum_percentage"].asInt(), 51);
}

// Канонический кейс аудита #3: частичная {power_distribution: SHARES}
// в организации с CONSENT -> -32005 на создании предложения.
TEST(ConfigDelta, SharesOnlyDeltaOnConsentOrgRejected) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("shares-delta-admin");
    Json::Value orgOut = createOrg(*h, creator, "Consent Only Org", "OPEN",
                                   orgConfigArgs("CONSENT", 600));
    std::string orgId = orgOut["org_id"].asString();

    Json::Value delta;
    delta["power_distribution"] = "SHARES";
    Json::Value pOut = deltaProposal(*h, creator, orgId, delta);
    ASSERT_TRUE(h->isError(pOut));
    EXPECT_EQ(h->errorCode(pOut), -32005);

    // Предложение не создано.
    Json::Value listArgs;
    listArgs["org_id"] = orgId;
    Json::Value listing = h->call("get_proposals", listArgs, &creator);
    EXPECT_EQ(listing.size(), 0u);

    // Конфигурация организации не изменилась.
    auto org = h->app->orgs->get(orgId);
    ASSERT_TRUE(org.has_value());
    EXPECT_EQ(org->config.power_distribution, PowerDistribution::EQUAL);
    EXPECT_EQ(org->config.consensus_model, ConsensusModel::CONSENT);
}

// Полная дельта по-прежнему применяется всеми четырьмя полями (регресс).
TEST(ConfigDelta, FullDeltaAppliesEveryField) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("full-delta-admin");
    Json::Value orgOut = createOrg(*h, creator, "Full Delta Org", "OPEN",
                                   orgConfigArgs("MAJORITY", 600));
    std::string orgId = orgOut["org_id"].asString();

    Json::Value delta = orgConfigArgs("QUORUM_PERCENTAGE", 1234, "SHARES", 60);
    Json::Value pOut = deltaProposal(*h, creator, orgId, delta);
    ASSERT_FALSE(h->isError(pOut));
    std::string pid = pOut["proposal_id"].asString();
    Json::Value receipt = vote(*h, creator, pid, "YES");
    ASSERT_EQ(receipt["proposal_status"].asString(), "PASSED");

    auto org = h->app->orgs->get(orgId);
    ASSERT_TRUE(org.has_value());
    EXPECT_EQ(org->config.consensus_model, ConsensusModel::QUORUM_PERCENTAGE);
    EXPECT_EQ(org->config.quorum_percentage, 60);
    EXPECT_EQ(org->config.voting_duration_sec, 1234);
    EXPECT_EQ(org->config.power_distribution, PowerDistribution::SHARES);
}

// Спека consensus-engine «Смена модели консенсуса через голосование»:
// активное предложение продолжает жить и закрывается по ПРЕЖНЕЙ модели.
TEST(ConfigDelta, ActiveProposalContinuesUnderOldRulesAfterConfigChange) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("old-rules-admin");
    // CONSENT: допустимы ABSTAIN; истечение при только-abstain даёт EXPIRED.
    Json::Value orgOut = createOrg(*h, creator, "Old Rules Org", "OPEN",
                                   orgConfigArgs("CONSENT", 600));
    std::string orgId = orgOut["org_id"].asString();

    // A — активное предложение под старой моделью CONSENT.
    std::string pidA = createProposal(*h, creator, orgId, "old rules proposal")["proposal_id"].asString();

    // Смена модели организации на MAJORITY через прошедшую дельту.
    Json::Value delta = orgConfigArgs("MAJORITY", 600);
    std::string pidCfg = deltaProposal(*h, creator, orgId, delta, "switch to majority")["proposal_id"].asString();
    Json::Value cfgReceipt = vote(*h, creator, pidCfg, "YES");
    ASSERT_EQ(cfgReceipt["proposal_status"].asString(), "PASSED");

    // ABSTAIN в предложении A принимается по СТАРОЙ модели
    // (новая MAJORITY запретила бы это решение).
    Json::Value receipt = vote(*h, creator, pidA, "ABSTAIN");
    ASSERT_FALSE(h->isError(receipt)) << receipt.toStyledString();

    // Истечение закрывает A по правилам CONSENT: EXPIRED,
    // а не REJECTED (как сделала бы MAJORITY на истечении).
    h->clock.advanceSeconds(700);
    h->app->engine->closeExpired(h->clock.nowSec());

    auto a = h->app->proposals->get(orgId, pidA);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->status, ProposalStatus::EXPIRED)
        << "предложение A обязано закрыться по прежней (CONSENT) модели";

    // Новые предложения создаются уже по новой модели.
    std::string pidNew = createProposal(*h, creator, orgId, "new rules")["proposal_id"].asString();
    auto fresh = h->app->proposals->get(orgId, pidNew);
    ASSERT_TRUE(fresh.has_value());
    EXPECT_EQ(fresh->config_at_creation.consensus_model, ConsensusModel::MAJORITY);
}
