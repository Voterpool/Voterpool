#include "core/Metrics.h"
#include "storage/RocksDBWrapper.h"
#include "tests/common/Harness.h"

#include <gtest/gtest.h>

#include <rocksdb/write_batch.h>

#include <string>

using namespace voterpool;

namespace {

std::string exposeAfterDefaults() {
    MetricsRegistry::instance().resetForTests();
    MetricsRegistry::instance().registerDefaults();
    return MetricsRegistry::instance().expose();
}

std::size_t posOf(const std::string& text, const std::string& needle) {
    return text.find(needle);
}

}  // namespace

TEST(MetricsExposition, HelpPrintedBeforeTypeForEveryFamily) {
    const std::string out = exposeAfterDefaults();

    static const char* kFamilies[] = {
        "voterpool_actions_applied_total",
        "voterpool_agents_total",
        "voterpool_consensus_early_exit_total",
        "voterpool_db_healthy",
        "voterpool_mcp_requests_total",
        "voterpool_orgs_active",
        "voterpool_proposals_closed_total",
        "voterpool_sse_connections",
        "voterpool_ttl_scans_total",
        "voterpool_votes_cast_total",
    };
    for (const char* family : kFamilies) {
        const std::string help = std::string("# HELP ") + family + " ";
        const std::string type = std::string("# TYPE ") + family + " ";
        ASSERT_NE(posOf(out, help), std::string::npos) << "missing HELP for " << family;
        ASSERT_NE(posOf(out, type), std::string::npos) << "missing TYPE for " << family;
        EXPECT_LT(posOf(out, help), posOf(out, type)) << "HELP must precede TYPE for " << family;
    }
}

TEST(MetricsExposition, HelpTextIsHumanReadableNotTheName) {
    const std::string out = exposeAfterDefaults();

    EXPECT_NE(out.find("# HELP voterpool_consensus_early_exit_total Early-Exit optimization triggers during voting\n"),
              std::string::npos);
    EXPECT_NE(out.find("# HELP voterpool_agents_total Registered agents\n"), std::string::npos);
    EXPECT_EQ(out.find("# HELP voterpool_agents_total voterpool_agents_total\n"), std::string::npos);
}

TEST(MetricsExposition, AgentsTotalExposedAsGauge) {
    const std::string out = exposeAfterDefaults();
    EXPECT_NE(out.find("# TYPE voterpool_agents_total gauge\n"), std::string::npos);
}

TEST(MetricsExposition, HistogramsGetHelpBeforeType) {
    MetricsRegistry::instance().resetForTests();
    MetricsRegistry::instance().registerDefaults();
    MetricsRegistry::instance().observe("voterpool_http_request_duration_seconds", 0.001);

    const std::string out = MetricsRegistry::instance().expose();
    const auto help = posOf(out, "# HELP voterpool_http_request_duration_seconds POST /mcp request latency\n");
    const auto type = posOf(out, "# TYPE voterpool_http_request_duration_seconds histogram\n");
    ASSERT_NE(help, std::string::npos);
    ASSERT_NE(type, std::string::npos);
    EXPECT_LT(help, type);
}

// observability: семейства voterpool_rocksdb_* появляются в выдаче после
// публикации тикеров, с HELP перед TYPE, без лейблов и с неотрицательными
// значениями (design D4).
TEST(MetricsExposition, RocksdbFamiliesExposedWithoutLabels) {
    MetricsRegistry::instance().resetForTests();
    MetricsRegistry::instance().registerDefaults();

    static const char* kRocksdbFamilies[] = {
        "voterpool_rocksdb_block_cache_usage",
        "voterpool_rocksdb_block_cache_capacity",
        "voterpool_rocksdb_block_cache_hits_total",
        "voterpool_rocksdb_block_cache_misses_total",
        "voterpool_rocksdb_estimate_pending_compaction_bytes",
        "voterpool_rocksdb_wal_synced_total",
        "voterpool_rocksdb_flush_write_bytes_total",
        "voterpool_rocksdb_compaction_read_bytes_total",
        "voterpool_rocksdb_compaction_write_bytes_total",
    };

    StorageConfig cfg;
    cfg.path = voterpool::testing::tempDbDir();
    RocksDBWrapper db(cfg);
    ASSERT_TRUE(db.open());
    {
        rocksdb::WriteBatch batch;
        db.put(batch, "default", "probe-key", "probe-value");
        ASSERT_TRUE(db.commit(batch));
    }
    db.publishStatisticsToRegistry();

    const std::string out = MetricsRegistry::instance().expose();
    for (const char* family : kRocksdbFamilies) {
        const std::string name(family);
        const auto help = posOf(out, "# HELP " + name + " ");
        const auto type = posOf(out, "# TYPE " + name + " ");
        ASSERT_NE(help, std::string::npos) << "missing HELP for " << name;
        ASSERT_NE(type, std::string::npos) << "missing TYPE for " << name;
        EXPECT_LT(help, type) << "HELP must precede TYPE for " << name;
        EXPECT_EQ(out.find(name + "{"), std::string::npos)
            << name << " must be label-free";
        const auto sample = posOf(out, "\n" + name + " ");
        ASSERT_NE(sample, std::string::npos) << "missing sample for " << name;
        const std::size_t valueStart = sample + name.size() + 2;
        const std::size_t valueEnd = out.find('\n', valueStart);
        EXPECT_GE(std::stoll(out.substr(valueStart, valueEnd - valueStart)), 0)
            << name << " value must be non-negative";
    }
    db.close();
}
