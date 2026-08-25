#include "storage/OrgNameRegistry.h"

#include "core/Config.h"
#include "domain/Organization.h"
#include "storage/Codec.h"
#include "storage/Keys.h"
#include "storage/RocksDBWrapper.h"
#include "tests/common/Harness.h"

#include <gtest/gtest.h>

using namespace voterpool;
using namespace voterpool::testing;

namespace {

void seedOrg(RocksDBWrapper& db, const std::string& id, const std::string& name, OrgStatus status) {
    Organization org;
    org.org_id = id;
    org.name = name;
    org.status = status;
    rocksdb::WriteBatch batch;
    db.put(batch, "cf_organizations", Keys::org(id), Codec::serializeOrg(org));
    ASSERT_TRUE(db.commit(batch));
}

}  // namespace

TEST(OrgNameRegistry, MatchQueryFindsSubstringAnywhereCaseInsensitive) {
    OrgNameRegistry r;
    r.add("id-1", "AI Council");
    r.add("id-2", "Dev Guild");
    r.add("id-3", "Blockchain Council of Research");

    auto hit = r.matchQuery("council");
    EXPECT_EQ(hit.size(), 2u);
    EXPECT_TRUE(hit.count("id-1"));
    EXPECT_TRUE(hit.count("id-3"));

    // Хвост названия: лексикографически «до» префикса запроса.
    EXPECT_EQ(r.matchQuery("research").size(), 1u);
    EXPECT_EQ(r.matchQuery("dev").size(), 1u);
    EXPECT_TRUE(r.matchQuery("missing").empty());
    EXPECT_TRUE(r.matchQuery("").empty());

    EXPECT_EQ(r.matchQuery("COUNCIL OF").size(), 1u);
}

TEST(OrgNameRegistry, FindAddRenameEraseLifecycle) {
    OrgNameRegistry r;
    r.add("id-1", "Alpha");
    EXPECT_EQ(r.findActiveByName("alpha").value_or(""), "id-1");

    r.rename("id-1", "Omega Prime");
    EXPECT_FALSE(r.findActiveByName("alpha").has_value());
    EXPECT_EQ(r.findActiveByName("omega prime").value_or(""), "id-1");

    r.rename("unknown-id", "Ghost");
    EXPECT_EQ(r.size(), 1u);

    r.erase("id-1");
    EXPECT_FALSE(r.findActiveByName("omega prime").has_value());
    EXPECT_EQ(r.size(), 0u);
    r.erase("id-1");  // повторное удаление безопасно
}

TEST(OrgNameRegistry, AddIsIdempotentPerOrgId) {
    OrgNameRegistry r;
    r.add("id-1", "Alpha");
    r.add("id-1", "Duplicate Insert");
    EXPECT_EQ(r.size(), 1u);
    EXPECT_EQ(r.findActiveByName("alpha").value_or(""), "id-1");
}

TEST(OrgNameRegistry, LoadReadsOnlyActiveOrgs) {
    StorageConfig cfg;
    cfg.path = tempDbDir();
    RocksDBWrapper db(cfg);
    ASSERT_TRUE(db.open());
    seedOrg(db, "active-1", "Live Council", OrgStatus::ACTIVE);
    seedOrg(db, "dead-1", "Doomed Council", OrgStatus::DISSOLVED);

    OrgNameRegistry r;
    r.load(db);
    EXPECT_EQ(r.size(), 1u);
    auto hit = r.matchQuery("council");
    EXPECT_EQ(hit.size(), 1u);
    EXPECT_TRUE(hit.count("active-1"));
    EXPECT_FALSE(r.findActiveByName("doomed council").has_value());
    db.close();
}
