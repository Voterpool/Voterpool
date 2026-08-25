#include "core/CryptoUtil.h"
#include "server/AppContext.h"
#include "storage/Keys.h"
#include "storage/SchemaVersion.h"
#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

using namespace voterpool;
using namespace voterpool::testing;

namespace {

// Пишет v1-базу с двумя валидными и одной битой записью.
void writeRawV1DbWithCorruptRecord(const std::string& dir, std::vector<std::string>& validKeys,
                                   std::string& corruptKey) {
    StorageConfig cfg;
    cfg.path = dir;
    RocksDBWrapper db(cfg);
    ASSERT_TRUE(db.open());
    rocksdb::WriteBatch batch;
    db.put(batch, "default", kSchemaVersionKey, "1");
    for (int i = 0; i < 2; ++i) {
        Json::Value legacy;
        legacy["proposal_id"] = generateUuidV4();
        legacy["org_id"] = generateUuidV4();
        legacy["creator_id"] = generateUuidV4();
        legacy["title"] = i == 0 ? "valid one" : "valid two";
        legacy["type"] = "STANDARD";
        legacy["status"] = "ACTIVE";
        legacy["created_at"] = 1700000000;
        legacy["expires_at"] = 1700000600;
        legacy["yes_power"] = 0.0;
        legacy["no_power"] = 0.0;
        legacy["abstain_power"] = 0.0;
        legacy["voters_count"] = 0;
        legacy["total_voting_power_at_creation"] = 10.0 + i;
        std::string key = Keys::proposal(legacy["org_id"].asString(), legacy["proposal_id"].asString());
        db.put(batch, "cf_proposals", key, Codec::dump(legacy));
        validKeys.push_back(key);
    }
    corruptKey = Keys::proposal(generateUuidV4(), generateUuidV4());
    db.put(batch, "cf_proposals", corruptKey, "{not-valid-json!!");
    ASSERT_TRUE(db.commit(batch));
    db.close();
}

std::optional<Json::Value> readProposal(RocksDBWrapper& db, const std::string& key) {
    auto v = db.get("cf_proposals", key);
    if (!v) return std::nullopt;
    return Codec::parse(*v);
}

int schemaVersionOnDisk(const std::string& dir) {
    StorageConfig cfg;
    cfg.path = dir;
    RocksDBWrapper db(cfg);
    if (!db.open()) return -1;
    int v = -1;
    if (auto raw = db.get("default", kSchemaVersionKey)) v = std::stoi(*raw);
    db.close();
    return v;
}

// Фейлящийся старт без Harness: его деструктор удаляет директорию с данными.
void initAppContextExpectThrow(const AppConfig& cfg) {
    AppContext ctx;
    ctx.config = cfg;
    EXPECT_THROW(ctx.init(nullptr), std::runtime_error);
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

    // v1-запись с неизвестным полем, без config_at_creation.
    StorageConfig rawCfg;
    rawCfg.path = dir;
    RocksDBWrapper db(rawCfg);
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

    AppConfig cfg;
    cfg.storage.path = dir;
    auto h = Harness::create(cfg);

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

TEST(Migration, CorruptRecordAbortsMigrationWithoutWrites) {
    std::string dir = tempDbDir();
    std::vector<std::string> validKeys;
    std::string corruptKey;
    writeRawV1DbWithCorruptRecord(dir, validKeys, corruptKey);

    StorageConfig cfg;
    cfg.path = dir;
    RocksDBWrapper db(cfg);
    ASSERT_TRUE(db.open());
    SchemaManager schema(db);
    EXPECT_EQ(schema.run(), SchemaGateResult::kError);

    // Версия не повышена, диск не тронут: соседние записи без нового поля,
    // битая запись — в исходных байтах.
    auto version = db.get("default", kSchemaVersionKey);
    ASSERT_TRUE(version.has_value());
    EXPECT_EQ(std::stoi(*version), 1);

    for (const auto& key : validKeys) {
        auto json = readProposal(db, key);
        ASSERT_TRUE(json.has_value()) << key;
        EXPECT_FALSE(json->isMember("config_at_creation")) << "disk must stay untouched: " << key;
    }
    auto raw = db.get("cf_proposals", corruptKey);
    ASSERT_TRUE(raw.has_value());
    EXPECT_EQ(*raw, "{not-valid-json!!");
    db.close();
}

TEST(Migration, InitThrowsOnMigrationError) {
    std::string dir = tempDbDir();
    std::vector<std::string> validKeys;
    std::string corruptKey;
    writeRawV1DbWithCorruptRecord(dir, validKeys, corruptKey);

    AppConfig cfg;
    cfg.storage.path = dir;
    DbHealth::instance().reset();
    initAppContextExpectThrow(cfg);
    DbHealth::instance().reset();

    // Старт фатален: версия на диске осталась 1.
    EXPECT_EQ(schemaVersionOnDisk(dir), 1);
    std::filesystem::remove_all(dir);
}

TEST(Migration, RestartAfterFixingCorruptionCompletesMigration) {
    std::string dir = tempDbDir();
    std::vector<std::string> validKeys;
    std::string corruptKey;
    writeRawV1DbWithCorruptRecord(dir, validKeys, corruptKey);

    // Первый старт падает фатально (без Harness — см. InitThrowsOnMigrationError).
    AppConfig cfgFirst;
    cfgFirst.storage.path = dir;
    DbHealth::instance().reset();
    initAppContextExpectThrow(cfgFirst);
    DbHealth::instance().reset();

    // Оператор удаляет битую запись (после разбора/восстановления из checkpoint).
    {
        StorageConfig rawCfg;
        rawCfg.path = dir;
        RocksDBWrapper raw(rawCfg);
        ASSERT_TRUE(raw.open());
        rocksdb::WriteBatch batch;
        raw.remove(batch, "cf_proposals", corruptKey);
        ASSERT_TRUE(raw.commit(batch));
        raw.close();
    }

    // Повторный старт (Harness): миграция выполняется заново и завершается.
    AppConfig cfgSecond;
    cfgSecond.storage.path = dir;
    auto h = Harness::create(cfgSecond);
    ASSERT_NE(h->app->db, nullptr);

    // Уже преобразованные записи мигрированы ровно один раз, дефолты на месте.
    for (const auto& key : validKeys) {
        auto json = readProposal(*h->app->db, key);
        ASSERT_TRUE(json.has_value()) << key;
        ASSERT_TRUE(json->isMember("config_at_creation")) << key;
        EXPECT_EQ((*json)["config_at_creation"]["consensus_model"].asString(), "MAJORITY");
        EXPECT_EQ((*json)["config_at_creation"]["quorum_percentage"].asInt(), 51);
    }
    auto version = h->app->db->get("default", kSchemaVersionKey);
    ASSERT_TRUE(version.has_value());
    EXPECT_EQ(std::stoi(*version), VOTERPOOL_SCHEMA_VERSION);
}
