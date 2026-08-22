#include "server/NativeAuthProvider.h"
#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

using namespace voterpool;
using namespace voterpool::testing;

TEST(AuthNative, RegisterIssuesPermanentTokenStoredOnlyAsHash) {
    auto h = Harness::create();
    AgentContext a = h->registerAgent("auth-user");
    const std::string& apiKey = Harness::ApiKeyStore::instance().get(a.agent_id);
    EXPECT_TRUE(apiKey.rfind("voterpool_sec_", 0) == 0);

    auto agent = h->app->agents->get(a.agent_id);
    ASSERT_TRUE(agent.has_value());
    EXPECT_EQ(agent->api_key_hash, sha256Hex(apiKey));
    EXPECT_NE(agent->api_key_hash, apiKey);

    auto it = h->app->db->newIterator("cf_auth");
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        std::string v = it->value().ToString();
        EXPECT_EQ(v, a.agent_id) << "cf_auth maps hash -> agent_id only";
        EXPECT_TRUE(v.find("voterpool_sec_") == std::string::npos);
    }
}

TEST(AuthNative, ValidTokenResolvesAgentInvalidRejected) {
    auto h = Harness::create();
    AgentContext a = h->registerAgent("auth-check");

    NativeAuthProvider provider(*h->app->agents);
    auto ok = provider.validate(Harness::ApiKeyStore::instance().get(a.agent_id));
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(ok->agent_id, a.agent_id);
    EXPECT_EQ(ok->auth_provider, "NATIVE");

    EXPECT_FALSE(provider.validate("voterpool_sec_totallywrong").has_value());
    EXPECT_FALSE(provider.validate("").has_value());
    EXPECT_FALSE(provider.validate("garbage").has_value());
}

namespace {
size_t h2_probe_organizations(RocksDBWrapper& db, const std::string& agentId) {
    MockClock clockProbe{1700000000};
    OrgRepository orgs(db, clockProbe);
    return orgs.listOrgsOfAgent(agentId).size();
}
}  // namespace

TEST(AuthNative, IdentityPersistsAcrossReopenWithoutReregistration) {
    std::string dir = tempDbDir();
    AppConfig cfg;
    cfg.storage.path = dir;

    AgentContext a;
    std::string token;
    auto h = Harness::create(cfg);
    {
        a = h->registerAgent("persistent");
        token = Harness::ApiKeyStore::instance().get(a.agent_id);

        Json::Value orgArgs;
        orgArgs["name"] = "Persistent Org";
        orgArgs["type"] = "OPEN";
        orgArgs["config"] = orgConfigArgs("MAJORITY", 600);
        ASSERT_FALSE(h->isError(h->call("create_organization", orgArgs, &a)));

        if (h->app->workers) h->app->workers->stop();
        h->app->workers.reset();
        h->app->engine.reset();
        h->app->hub.reset();
        h->app->db->close();
        h->app.reset();

        StorageConfig reopenCfg;
        reopenCfg.path = dir;
        RocksDBWrapper reopened(reopenCfg);
        ASSERT_TRUE(reopened.open());
        MockClock reopenClock{1700000000};
        AgentRepository reopenedAgents(reopened, reopenClock);

        NativeAuthProvider provider(reopenedAgents);
        auto ctx = provider.validate(token);
        ASSERT_TRUE(ctx.has_value());
        EXPECT_EQ(ctx->agent_id, a.agent_id);

        size_t agentsOnDisk = 0;
        {
            auto it = reopened.newIterator("default");
            for (it->Seek("agent:"); it->Valid() && it->key().ToString().rfind("agent:", 0) == 0; it->Next())
                ++agentsOnDisk;
        }
        EXPECT_EQ(agentsOnDisk, 1u);
        EXPECT_EQ(h2_probe_organizations(reopened, a.agent_id), 1u);

        reopened.close();
    }
}


