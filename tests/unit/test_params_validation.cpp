#include "core/CryptoUtil.h"
#include "mcp/McpHandler.h"
#include "mcp/tools/ToolDefs.h"
#include "mcp/tools/ToolHelpers.h"
#include "tests/common/Harness.h"

using namespace voterpool;
using namespace voterpool::testing;


TEST(ParamsValidation, RegisterAgentRequiresName) {
    auto h = Harness::create();
    Json::Value out = h->call("register_agent", Json::Value(Json::objectValue), nullptr);
    EXPECT_TRUE(h->isError(out));
    EXPECT_EQ(h->errorCode(out), -32602);
}

TEST(ParamsValidation, InvalidUuidRejectedWith32602) {
    auto h = Harness::create();
    AgentContext a = h->registerAgent("uuid-checker");
    Harness::ApiKeyStore::instance();
    Json::Value args;
    args["org_id"] = "not-a-uuid";
    Json::Value out = h->call("get_organization", args, &a);
    EXPECT_EQ(h->errorCode(out), -32602);
}

TEST(ParamsValidation, UnknownToolYieldsMethodNotFound) {
    auto h = Harness::create();
    Json::Value out = h->call("no_such_tool", Json::Value(Json::objectValue), nullptr);
    EXPECT_EQ(h->errorCode(out), -32601);
}

TEST(ParamsValidation, AuthRequiredForProtectedTools) {
    auto h = Harness::create();
    Json::Value args;
    args["name"] = "Org";
    args["type"] = "OPEN";
    Json::Value cfg;
    cfg["consensus_model"] = "MAJORITY";
    cfg["voting_duration_sec"] = 60;
    args["config"] = cfg;
    Json::Value out = h->call("create_organization", args, nullptr);
    EXPECT_EQ(h->errorCode(out), -32001);
}

TEST(ParamsValidation, BothConfigDeltaAndActionRejected) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("dual-proposer");
    Json::Value orgArgs, cfg;
    orgArgs["name"] = "Dual Org";
    orgArgs["type"] = "OPEN";
    cfg["consensus_model"] = "MAJORITY";
    cfg["voting_duration_sec"] = 600;
    orgArgs["config"] = cfg;
    Json::Value orgOut = h->call("create_organization", orgArgs, &creator);

    Json::Value pArgs;
    pArgs["org_id"] = orgOut["org_id"].asString();
    pArgs["title"] = "t";
    Json::Value delta = cfg;
    delta["voting_duration_sec"] = 120;
    pArgs["config_delta"] = delta;
    Json::Value action;
    action["kind"] = "APPROVE_MEMBER";
    Json::Value payload;
    payload["target_agent_id"] = generateUuidV4();
    action["payload"] = payload;
    pArgs["action"] = action;

    Json::Value out = h->call("create_proposal", pArgs, &creator);
    EXPECT_TRUE(h->isError(out));
    EXPECT_EQ(h->errorCode(out), -32005);
}

TEST(ParamsValidation, UnknownActionKindRejected) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("kind-proposer");
    Json::Value orgArgs, cfg;
    orgArgs["name"] = "Kind Org";
    orgArgs["type"] = "OPEN";
    cfg["consensus_model"] = "CONSENT";
    cfg["voting_duration_sec"] = 600;
    orgArgs["config"] = cfg;
    Json::Value orgOut = h->call("create_organization", orgArgs, &creator);

    Json::Value pArgs;
    pArgs["org_id"] = orgOut["org_id"].asString();
    pArgs["title"] = "t";
    Json::Value action;
    action["kind"] = "NUKE_EVERYTHING";
    pArgs["action"] = action;
    Json::Value out = h->call("create_proposal", pArgs, &creator);
    EXPECT_EQ(h->errorCode(out), -32602);
}

TEST(CryptoUtils, UuidAndTokenFormats) {
    std::string id = generateUuidV4();
    EXPECT_TRUE(isValidUuid(id));
    EXPECT_FALSE(isValidUuid(id + "x"));
    EXPECT_TRUE(generateApiKey().rfind("voterpool_sec_", 0) == 0);
    EXPECT_EQ(generateApiKey().size(), std::string("voterpool_sec_").size() + 48);
}

namespace {

OrgConfig consentBase() {
    OrgConfig c;
    c.consensus_model = ConsensusModel::CONSENT;
    c.quorum_percentage = 51;
    c.voting_duration_sec = 3600;
    c.power_distribution = PowerDistribution::EQUAL;
    return c;
}

}  // namespace

TEST(OrgConfigMerge, PartialDeltaInheritsBaseFields) {
    Json::Value delta;
    delta["voting_duration_sec"] = 7200;
    OrgConfig base = consentBase();
    auto r = mcp::parseOrgConfig(delta, true, &base);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().consensus_model, ConsensusModel::CONSENT);
    EXPECT_EQ(r.value().power_distribution, PowerDistribution::EQUAL);
    EXPECT_EQ(r.value().quorum_percentage, 51);
    EXPECT_EQ(r.value().voting_duration_sec, 7200);
}

TEST(OrgConfigMerge, SharesOnConsentOrgRejectedThroughMerge) {
    Json::Value partial;
    partial["power_distribution"] = "SHARES";
    OrgConfig base = consentBase();
    auto r = mcp::parseOrgConfig(partial, true, &base);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, -32005);

    Json::Value full;
    full["consensus_model"] = "CONSENT";
    full["power_distribution"] = "SHARES";
    r = mcp::parseOrgConfig(full, true, &base);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, -32005);
}

TEST(OrgConfigMerge, InvalidValuesThroughMergeRejected) {
    OrgConfig base = consentBase();

    Json::Value quorum;
    quorum["quorum_percentage"] = 150;
    EXPECT_EQ(mcp::parseOrgConfig(quorum, true, &base).error().code, -32602);

    Json::Value duration;
    duration["voting_duration_sec"] = 0;
    EXPECT_EQ(mcp::parseOrgConfig(duration, true, &base).error().code, -32005);

    Json::Value badModel;
    badModel["consensus_model"] = "ANARCHY";
    EXPECT_EQ(mcp::parseOrgConfig(badModel, true, &base).error().code, -32602);

    Json::Value badDist;
    badDist["power_distribution"] = "PLUTOCRACY";
    EXPECT_EQ(mcp::parseOrgConfig(badDist, true, &base).error().code, -32602);
}

TEST(OrgConfigMerge, WithoutBaseLegacyDefaultsPreserved) {
    Json::Value partial;
    partial["power_distribution"] = "SHARES";
    auto r = mcp::parseOrgConfig(partial, true);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().consensus_model, ConsensusModel::MAJORITY);
    EXPECT_EQ(r.value().quorum_percentage, 51);
    EXPECT_EQ(r.value().voting_duration_sec, 3600);
}
