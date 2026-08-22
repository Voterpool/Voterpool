#include "storage/Checkpoint.h"
#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

#include <fstream>

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
