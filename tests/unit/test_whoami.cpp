// whoami: самодиагностика идентичности и мульти-бакетная консистентность;
// register_agent: предупреждение о второй личности под живым токеном.
// (change improve-agent-onboarding-contracts, tasks 3.1-3.2)
#include "mcp/tools/ToolHelpers.h"
#include "scaling/BucketResolver.h"
#include "storage/Keys.h"
#include "tests/common/Harness.h"

#include <gtest/gtest.h>

#include <fstream>
#include <json/json.h>
#include <sstream>
#include <string>
#include <vector>

using namespace voterpool;
using namespace voterpool::testing;

#ifdef VOTERPOOL_FIXTURES_DIR
#define FIXTURES_DIR VOTERPOOL_FIXTURES_DIR
#else
#define FIXTURES_DIR "../fixtures"
#endif

namespace {
std::vector<std::pair<std::string, int>> loadBoundaryVectors() {
    std::ifstream in(std::string(FIXTURES_DIR) + "/bucket_vectors.json");
    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    Json::parseFromStream(rb, in, &root, &errs);
    std::vector<std::pair<std::string, int>> out;
    for (const auto& v : root["vectors"]) {
        if (v["boundary"].asBool()) out.emplace_back(v["org_id"].asString(), v["bucket"].asInt());
    }
    return out;
}

Organization seededOrg(const std::string& id, const std::string& name) {
    Organization org;
    org.org_id = id;
    org.name = name;
    org.type = OrgType::OPEN;
    org.status = OrgStatus::ACTIVE;
    org.created_at = org.updated_at = 1700000000;
    org.config.consensus_model = ConsensusModel::MAJORITY;
    org.config.voting_duration_sec = 600;
    org.config.power_distribution = PowerDistribution::EQUAL;
    org.total_voting_power = 1.0;
    return org;
}
}  // namespace

TEST(Whoami, FreshAgentHasEmptyMembershipsAndPending) {
    auto h = Harness::create();
    AgentContext a = h->registerAgent("whoami-fresh");
    Json::Value out = h->call("whoami", Json::Value(Json::objectValue), &a);
    ASSERT_FALSE(h->isError(out)) << out["message"].asString();
    EXPECT_EQ(out["agent_id"].asString(), a.agent_id);
    EXPECT_EQ(out["memberships"].size(), 0u);
    EXPECT_EQ(out["pending"].size(), 0u);
}

TEST(Whoami, ListsActiveMembershipWithOrgName) {
    auto h = Harness::create();
    AgentContext a = h->registerAgent("whoami-member");
    Json::Value orgArgs;
    orgArgs["name"] = "Who Am I Org";
    orgArgs["type"] = "OPEN";
    Json::Value cfg;
    cfg["consensus_model"] = "MAJORITY";
    cfg["voting_duration_sec"] = 600;
    orgArgs["config"] = cfg;
    Json::Value orgOut = h->call("create_organization", orgArgs, &a);
    ASSERT_FALSE(h->isError(orgOut));

    Json::Value out = h->call("whoami", Json::Value(Json::objectValue), &a);
    ASSERT_FALSE(h->isError(out));
    ASSERT_EQ(out["memberships"].size(), 1u);
    EXPECT_EQ(out["memberships"][0]["org_id"].asString(), orgOut["org_id"].asString());
    EXPECT_EQ(out["memberships"][0]["name"].asString(), "Who Am I Org");
    EXPECT_EQ(out["memberships"][0]["role"].asString(), "ADMIN");
    EXPECT_NEAR(out["memberships"][0]["voting_power"].asDouble(), 1.0, 1e-9);
    EXPECT_EQ(out["pending"].size(), 0u);
}

// Решающая проверка мульти-бакетности: связи в КРАЙНИХ бакетах (0 и 255)
// плюс середняк — все обязаны появиться в ответе.
TEST(Whoami, MultiBucketMembershipConsistency) {
    auto h = Harness::create();
    AgentContext a = h->registerAgent("whoami-multibucket");

    const auto vectors = loadBoundaryVectors();
    ASSERT_GE(vectors.size(), 2u);

    // Две граничные организации (бакеты из фикстуры — крайние 0/255 при
    // порядке обхода) + одна обычная со случайным бакетом.
    std::vector<std::string> seededIds;
    int used = 0;
    std::set<int> seenBuckets;
    for (const auto& [id, bucket] : vectors) {
        if (!seenBuckets.insert(bucket).second || seededIds.size() >= 2) continue;
        if (used == 0 && bucket != 0 && bucket != 255) continue;  // берём именно крайние
        Organization org = seededOrg(id, "Bucket Org " + std::to_string(seededIds.size()));
        rocksdb::WriteBatch batch;
        h->app->db->put(batch, "cf_organizations", Keys::org(id), Codec::serializeOrg(org));
        h->app->indexes->addFeedActive(batch, org);
        ASSERT_TRUE(h->app->db->commit(batch));
        h->app->directory->indexOrg(id, Keys::nameLower(org.name));

        Membership m;
        m.org_id = id;
        m.agent_id = a.agent_id;
        m.role = MemberRole::MEMBER;
        m.status = MemberStatus::ACTIVE;
        m.voting_power = 1.0;
        m.created_at = m.updated_at = 1700000000;
        rocksdb::WriteBatch mb;
        h->app->identity->recordMembershipLink(mb, m);
        ASSERT_TRUE(h->app->db->commit(mb));
        seededIds.push_back(id);
        ++used;
    }
    ASSERT_EQ(seededIds.size(), 2u);
    EXPECT_NE(scaling::bucketFor(seededIds[0]), scaling::bucketFor(seededIds[1]));

    Json::Value out = h->call("whoami", Json::Value(Json::objectValue), &a);
    ASSERT_FALSE(h->isError(out));
    ASSERT_EQ(out["memberships"].size(), 2u);
    // Детерминированный порядок: org_id asc.
    EXPECT_LT(out["memberships"][0]["org_id"].asString(),
              out["memberships"][1]["org_id"].asString());
    std::set<std::string> got;
    for (const auto& item : out["memberships"]) got.insert(item["org_id"].asString());
    for (const auto& id : seededIds) EXPECT_TRUE(got.count(id)) << id << " lost in bucket scan";
}

TEST(RegisterAgentWarning, AnonymousCallHasNoWarning) {
    auto h = Harness::create();
    Json::Value args;
    args["name"] = "fresh identity";
    Json::Value out = h->call("register_agent", args, nullptr);
    ASSERT_FALSE(h->isError(out));
    EXPECT_FALSE(out.isMember("identity_warning"));
}

TEST(RegisterAgentWarning, AuthenticatedCallerGetsIdentityWarning) {
    auto h = Harness::create();
    AgentContext existing = h->registerAgent("already-here");
    Json::Value args;
    args["name"] = "second identity";
    Json::Value out = h->call("register_agent", args, &existing);
    ASSERT_FALSE(h->isError(out));
    ASSERT_TRUE(out.isMember("identity_warning"));
    EXPECT_EQ(out["identity_warning"]["current_agent_id"].asString(), existing.agent_id);
}
