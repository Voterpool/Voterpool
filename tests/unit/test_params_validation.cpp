#include "core/CryptoUtil.h"
#include "mcp/McpHandler.h"
#include "mcp/tools/ToolDefs.h"
#include "tests/common/Harness.h"

using namespace voterpool;
using namespace voterpool::testing;

static std::shared_ptr<Harness> h;

static void SetUpOnce() {
    if (!h) h = Harness::create();
}

TEST(ParamsValidation, RegisterAgentRequiresName) {
    SetUpOnce();
    Json::Value out = h->call("register_agent", Json::Value(Json::objectValue), nullptr);
    EXPECT_TRUE(h->isError(out));
    EXPECT_EQ(h->errorCode(out), -32602);
}

TEST(ParamsValidation, InvalidUuidRejectedWith32602) {
    SetUpOnce();
    AgentContext a = h->registerAgent("uuid-checker");
    Harness::ApiKeyStore::instance();
    Json::Value args;
    args["org_id"] = "not-a-uuid";
    Json::Value out = h->call("get_organization", args, &a);
    EXPECT_EQ(h->errorCode(out), -32602);
}

TEST(ParamsValidation, UnknownToolYieldsMethodNotFound) {
    SetUpOnce();
    Json::Value out = h->call("no_such_tool", Json::Value(Json::objectValue), nullptr);
    EXPECT_EQ(h->errorCode(out), -32601);
}

TEST(ParamsValidation, AuthRequiredForProtectedTools) {
    SetUpOnce();
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
    SetUpOnce();
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
    SetUpOnce();
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
