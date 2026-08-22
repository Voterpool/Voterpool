#include "core/CryptoUtil.h"
#include "storage/Keys.h"
#include "storage/SchemaVersion.h"
#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

#include <fstream>

using namespace voterpool;
using namespace voterpool::testing;

namespace {

void writeRawV1Db(const std::string& dir) {
    StorageConfig cfg;
    cfg.path = dir;
    RocksDBWrapper db(cfg);
    ASSERT_TRUE(db.open());
    rocksdb::WriteBatch batch;
    db.put(batch, "default", kSchemaVersionKey, "1");
    Json::Value legacy;
    legacy["proposal_id"] = generateUuidV4();
    legacy["org_id"] = generateUuidV4();
    legacy["creator_id"] = generateUuidV4();
    legacy["title"] = "legacy proposal";
    legacy["type"] = "STANDARD";
    legacy["status"] = "ACTIVE";
    legacy["created_at"] = 1700000000;
    legacy["expires_at"] = 1700000600;
    legacy["yes_power"] = 0.0;
    legacy["no_power"] = 0.0;
    legacy["abstain_power"] = 0.0;
    legacy["voters_count"] = 0;
    legacy["total_voting_power_at_creation"] = 10.0;
    legacy["future_field"] = "keep-me";
    db.put(batch, "cf_proposals", Keys::proposal(legacy["org_id"].asString(), legacy["proposal_id"].asString()),
           Codec::dump(legacy));
    ASSERT_TRUE(db.commit(batch));
    db.close();
}

}  // namespace

TEST(Migration, FreshDatabaseInitializedAtCurrentVersion) {
    auto h = Harness::create();
    auto v = h->app->db->get("default", kSchemaVersionKey);
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(std::stoi(*v), VOTERPOOL_SCHEMA_VERSION);
}

TEST(Migration, V1RecordsBackfilledAndUnknownFieldsPreserved) {
    std::string dir = tempDbDir();
    writeRawV1Db(dir);

    AppConfig cfg;
    cfg.storage.path = dir;
    auto h = Harness::create(cfg);
    EXPECT_EQ(h->app->config.storage.path, dir);

    auto it = h->app->db->newIterator("cf_proposals");
    it->SeekToFirst();
    ASSERT_TRUE(it->Valid());
    auto json = Codec::parse(it->value().ToString());
    ASSERT_TRUE(json.has_value());
    EXPECT_TRUE(json->isMember("config_at_creation"));
    EXPECT_EQ((*json)["future_field"].asString(), "keep-me");

    auto version = h->app->db->get("default", kSchemaVersionKey);
    EXPECT_EQ(std::stoi(*version), VOTERPOOL_SCHEMA_VERSION);
}

TEST(Migration, NewerSchemaThanBinaryIsFatal) {
    std::string dir = tempDbDir();
    {
        StorageConfig cfg;
        cfg.path = dir;
        RocksDBWrapper db(cfg);
        ASSERT_TRUE(db.open());
        rocksdb::WriteBatch batch;
        db.put(batch, "default", kSchemaVersionKey, std::to_string(VOTERPOOL_SCHEMA_VERSION + 5));
        ASSERT_TRUE(db.commit(batch));
        db.close();
    }
    StorageConfig cfg;
    cfg.path = dir;
    RocksDBWrapper db(cfg);
    ASSERT_TRUE(db.open());
    SchemaManager schema(db);
    EXPECT_EQ(schema.run(), SchemaGateResult::kFatalNewerSchema);
    db.close();
    std::filesystem::remove_all(dir);
}
