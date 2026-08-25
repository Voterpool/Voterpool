#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

using namespace voterpool;
using namespace voterpool::testing;

TEST(Recovery, StateFullyRestoredAfterCloseReopen) {
    std::string dir = tempDbDir();
    AppConfig cfg;
    cfg.storage.path = dir;
    {
        auto h = Harness::create(cfg);

        AgentContext creator = h->registerAgent("recovery-admin");
        AgentContext voter = h->registerAgent("recovery-voter");

        Json::Value orgArgs;
        orgArgs["name"] = "Durable Org";
        orgArgs["type"] = "OPEN";
        orgArgs["config"] = orgConfigArgs("MAJORITY", 6000);
        Json::Value orgOut = h->call("create_organization", orgArgs, &creator);
        std::string orgId = orgOut["org_id"].asString();

        Json::Value joinArgs;
        joinArgs["org_id"] = orgId;
        h->call("join_organization", joinArgs, &voter);

        Json::Value pArgs;
        pArgs["org_id"] = orgId;
        pArgs["title"] = "durable";
        std::string pid = h->call("create_proposal", pArgs, &creator)["proposal_id"].asString();
        vote(*h, voter, pid, "YES");

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

        auto it = reopened.newIterator("default");
        size_t agentCount = 0;
        for (it->Seek("agent:"); it->Valid() && it->key().ToString().rfind("agent:", 0) == 0; it->Next())
            ++agentCount;
        EXPECT_EQ(agentCount, 2u);

        auto orgIt = reopened.newIterator("cf_organizations");
        orgIt->SeekToFirst();
        ASSERT_TRUE(orgIt->Valid());
        auto org = Codec::deserializeOrg(orgIt->value().ToString());
        ASSERT_TRUE(org.has_value());
        EXPECT_EQ(org->name, "Durable Org");
        EXPECT_DOUBLE_EQ(org->total_voting_power, 2.0);

        auto propIt = reopened.newIterator("cf_proposals");
        propIt->SeekToFirst();
        ASSERT_TRUE(propIt->Valid());
        auto prop = Codec::deserializeProposal(propIt->value().ToString());
        ASSERT_TRUE(prop.has_value());
        EXPECT_EQ(prop->status, ProposalStatus::ACTIVE);
        EXPECT_DOUBLE_EQ(prop->yes_power, 1.0);
        EXPECT_EQ(prop->voters_count, 1);

        it.reset();
        orgIt.reset();
        propIt.reset();
        reopened.close();
    }
}

// Спека background-workers «Фазы запуска и восстановления»: опциональное
// восстановление индекса активных предложений при старте не должно ронять
// процесс и должно давать рабочий индекс для TTL-воркера.
TEST(Recovery, RebuildIndexOnStartRestoresUsableActiveIndex) {
    std::string dir = tempDbDir();
    AppConfig cfg;
    cfg.storage.path = dir;
    auto h = Harness::create(cfg);
    std::string orgId;
    std::string pid;
    {
        AgentContext creator = h->registerAgent("rebuild-admin");
        Json::Value orgOut = createOrg(*h, creator, "Rebuild Org", "OPEN",
                                       orgConfigArgs("MAJORITY", 6000));
        ASSERT_FALSE(h->isError(orgOut));
        orgId = orgOut["org_id"].asString();
        pid = createProposal(*h, creator, orgId, "survivor")["proposal_id"].asString();

        // Индекс активности мог быть потерян (сценарий повреждения): чистим.
        {
            rocksdb::WriteBatch batch;
            auto pOpt = h->app->proposals->get(orgId, pid);
            ASSERT_TRUE(pOpt.has_value());
            h->app->proposals->removeActiveIndex(batch, *pOpt);
            ASSERT_TRUE(h->app->db->commit(batch));
        }

        if (h->app->workers) h->app->workers->stop();
        h->app->workers.reset();
        h->app->engine.reset();
        h->app->hub.reset();
        h->app->db->close();
        h->app.reset();
    }

    AppConfig rebuildCfg = cfg;
    rebuildCfg.storage.rebuild_index_on_start = true;
    AppContext restarted;
    restarted.config = rebuildCfg;
    restarted.init(&h->clock);  // до фикса здесь был гарантированный SIGSEGV

    // Восстановленный индекс пригоден: TTL-воркер находит и закрывает предложение.
    h->clock.advanceSeconds(7000);
    restarted.engine->closeExpired(h->clock.nowSec());

    auto p = restarted.proposals->get(orgId, pid);
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->status, ProposalStatus::REJECTED)
        << "MAJORITY без голосов на истечении -> REJECTED через восстановленный индекс";
    restarted.db->close();
}
