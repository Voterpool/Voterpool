#include "core/CryptoUtil.h"
#include "storage/Checkpoint.h"
#include "storage/Codec.h"
#include "storage/Keys.h"
#include "storage/RocksDBWrapper.h"
#include "storage/SchemaVersion.h"
#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

#include <rocksdb/write_batch.h>

#include <filesystem>
#include <fstream>
#include <vector>

using namespace voterpool;
using namespace voterpool::testing;

namespace {
std::string writeConfigFile(const std::string& dir, const std::string& dbPath) {
    std::string path = dir + "_config.yaml";
    std::ofstream out(path);
    out << "server:\n  port: 18080\nstorage:\n  path: \"" << dbPath << "\"\n";
    out.close();
    return path;
}
}  // namespace

TEST(CheckpointRestore, SnapshotRestoresFullState) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("backup-admin");
    Json::Value orgOut = createOrg(*h, creator, "Backed Up Org", "OPEN", orgConfigArgs("CONSENT", 600));
    std::string orgId = orgOut["org_id"].asString();

    std::string snapshotDir = tempDbDir() + "_snap";
    const std::string sourceDir = h->dir;

    if (h->app->workers) h->app->workers->stop();
    h->app->workers.reset();
    h->app->engine.reset();
    h->app->hub.reset();
    h->app->db->close();
    h->app.reset();

    std::string configPath = writeConfigFile(tempDbDir(), sourceDir);

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>("voterpool"));
    argv.push_back(const_cast<char*>("checkpoint"));
    argv.push_back(const_cast<char*>("--config"));
    argv.push_back(const_cast<char*>(configPath.c_str()));
    argv.push_back(const_cast<char*>("--path"));
    argv.push_back(const_cast<char*>(snapshotDir.c_str()));
    int rc = runCheckpointCommand(static_cast<int>(argv.size()), argv.data());
    ASSERT_EQ(rc, 0) << "checkpoint command failed";
    EXPECT_TRUE(std::filesystem::exists(snapshotDir + "/CURRENT"));

    StorageConfig restoredCfg;
    restoredCfg.path = snapshotDir;
    RocksDBWrapper restored(restoredCfg);
    ASSERT_TRUE(restored.open());
    {
        auto it = restored.newIterator("cf_organizations");
        it->SeekToFirst();
        ASSERT_TRUE(it->Valid());
        auto org = Codec::deserializeOrg(it->value().ToString());
        ASSERT_TRUE(org.has_value());
        EXPECT_EQ(org->name, "Backed Up Org");
    }
    restored.close();

    std::filesystem::remove_all(snapshotDir);
    std::filesystem::remove_all(h->dir);
}

TEST(CheckpointRestore, RefusesUnopenableDatabase) {
    std::string garbageDir = tempDbDir();
    std::filesystem::create_directories(garbageDir);
    { std::ofstream(garbageDir + "/CURRENT") << "garbage-not-a-manifest"; }

    std::string snapshotDir = tempDbDir() + "_snap2";
    std::string configPath = writeConfigFile(tempDbDir(), garbageDir);

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>("voterpool"));
    argv.push_back(const_cast<char*>("checkpoint"));
    argv.push_back(const_cast<char*>("--config"));
    argv.push_back(const_cast<char*>(configPath.c_str()));
    argv.push_back(const_cast<char*>("--path"));
    argv.push_back(const_cast<char*>(snapshotDir.c_str()));
    int rc = runCheckpointCommand(static_cast<int>(argv.size()), argv.data());
    EXPECT_EQ(rc, 1);
    std::filesystem::remove_all(garbageDir);
}

namespace {
// v1-фикстура (design D5): валидные legacy-записи предложений + версия
// схемы 1, без битых записей (по образцу хелпера test_migration.cpp).
void writeRawV1Db(const std::string& dir, std::vector<std::string>& keys) {
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
        legacy["title"] = i == 0 ? "v1 valid one" : "v1 valid two";
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
        keys.push_back(key);
    }
    ASSERT_TRUE(db.commit(batch));
    db.close();
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

int runCheckpoint(const std::string& configPath, const std::string& sourceDir,
                  const std::string& snapshotDir) {
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>("voterpool"));
    argv.push_back(const_cast<char*>("checkpoint"));
    argv.push_back(const_cast<char*>("--config"));
    argv.push_back(const_cast<char*>(configPath.c_str()));
    argv.push_back(const_cast<char*>("--path"));
    argv.push_back(const_cast<char*>(snapshotDir.c_str()));
    return runCheckpointCommand(static_cast<int>(argv.size()), argv.data());
}
}  // namespace

// data-persistence «Снимок старой схемы новым бинарником не мигрирует
// источник»: checkpoint не выполняет миграций — источник остаётся v1,
// миграция проходит на копии при восстановлении.
TEST(CheckpointRestore, SnapshotOfOlderSchemaLeavesSourceUntouched) {
    DbHealth::instance().reset();
    std::string sourceDir = tempDbDir();
    std::vector<std::string> keys;
    writeRawV1Db(sourceDir, keys);

    std::string workDir = tempDbDir();
    std::string configPath = writeConfigFile(workDir, sourceDir);
    std::string snapshotDir = tempDbDir() + "_snap_v1";

    ASSERT_EQ(runCheckpoint(configPath, sourceDir, snapshotDir), 0)
        << "checkpoint must succeed regardless of schema version";

    // Источник не тронут: версия осталась 1, записи не получили новое поле.
    EXPECT_EQ(schemaVersionOnDisk(sourceDir), 1) << "source must not be migrated";
    {
        StorageConfig cfg;
        cfg.path = sourceDir;
        RocksDBWrapper src(cfg);
        ASSERT_TRUE(src.open());
        for (const auto& key : keys) {
            auto json = Codec::parse(src.get("cf_proposals", key).value_or(""));
            ASSERT_TRUE(json.has_value()) << key;
            EXPECT_FALSE(json->isMember("config_at_creation"))
                << "disk must stay untouched: " << key;
        }
        src.close();
    }
    EXPECT_TRUE(std::filesystem::exists(snapshotDir + "/CURRENT"));

    // Восстановление из снимка: движок открывает копию и мигрирует её.
    {
        AppConfig restoredCfg;
        restoredCfg.storage.path = snapshotDir;
        auto h = Harness::create(restoredCfg);

        auto version = h->app->db->get("default", kSchemaVersionKey);
        ASSERT_TRUE(version.has_value());
        EXPECT_EQ(std::stoi(*version), VOTERPOOL_SCHEMA_VERSION);
        for (const auto& key : keys) {
            auto json = Codec::parse(h->app->db->get("cf_proposals", key).value_or(""));
            ASSERT_TRUE(json.has_value()) << key;
            EXPECT_TRUE(json->isMember("config_at_creation")) << key;
            EXPECT_EQ(json->isMember("future_field") ? (*json)["future_field"].asString() : "",
                      "") << "unexpected field";
        }
    }

    std::filesystem::remove_all(snapshotDir);
    std::filesystem::remove_all(sourceDir);
    std::filesystem::remove_all(workDir);
}

// Чистая семантика фотографии: снимается всё, что открывается, включая базу
// с версией схемы НОВЕЕ бинарника (гейт из команды убран).
TEST(CheckpointRestore, SnapshotOfNewerSchemaSucceeds) {
    DbHealth::instance().reset();
    std::string sourceDir = tempDbDir();
    {
        StorageConfig cfg;
        cfg.path = sourceDir;
        RocksDBWrapper db(cfg);
        ASSERT_TRUE(db.open());
        rocksdb::WriteBatch batch;
        db.put(batch, "default", kSchemaVersionKey, std::to_string(VOTERPOOL_SCHEMA_VERSION + 5));
        ASSERT_TRUE(db.commit(batch));
        db.close();
    }

    std::string workDir = tempDbDir();
    std::string configPath = writeConfigFile(workDir, sourceDir);
    std::string snapshotDir = tempDbDir() + "_snap_newer";

    EXPECT_EQ(runCheckpoint(configPath, sourceDir, snapshotDir), 0);
    EXPECT_EQ(schemaVersionOnDisk(snapshotDir), VOTERPOOL_SCHEMA_VERSION + 5)
        << "snapshot must preserve the schema version as-is";

    std::filesystem::remove_all(snapshotDir);
    std::filesystem::remove_all(sourceDir);
    std::filesystem::remove_all(workDir);
}
