#pragma once

#include "core/Config.h"
#include "core/Metrics.h"

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/write_batch.h>

#include <spdlog/spdlog.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace voterpool {

class DbHealth {
public:
    static DbHealth& instance() {
        static DbHealth h;
        return h;
    }
    bool healthy() const { return healthy_.load(std::memory_order_relaxed); }
    void markUnhealthy(const std::string& reason) {
        bool expected = true;
        if (healthy_.compare_exchange_strong(expected, false, std::memory_order_relaxed)) {
            MetricsRegistry::instance().setGauge("voterpool_db_healthy", {}, 0);
            spdlog::critical("Storage backend degraded: {}", reason);
        }
    }
    void reset() { healthy_.store(true, std::memory_order_relaxed); }

private:
    std::atomic<bool> healthy_{true};
};

class RocksDBWrapper {
public:
    explicit RocksDBWrapper(const StorageConfig& cfg, const std::string& dirOverride = "");
    ~RocksDBWrapper();

    RocksDBWrapper(const RocksDBWrapper&) = delete;
    RocksDBWrapper& operator=(const RocksDBWrapper&) = delete;

    bool open();
    void close();

    void publishBucketHistogram();
    bool isOpen() const { return db_ != nullptr; }

    void publishStatisticsToRegistry();

    static constexpr const char* kCfNames[] = {
        "default", "cf_organizations", "cf_memberships", "cf_proposals",
        "cf_votes", "cf_indexes", "cf_auth", "cf_agent_orgs", "cf_audit_log"};

    rocksdb::ColumnFamilyHandle* cf(const std::string& name) const;
    rocksdb::DB* raw() const { return db_.get(); }

    std::optional<std::string> get(const std::string& cf, const std::string& key);
    bool put(rocksdb::WriteBatch& batch, const std::string& cf, const std::string& key, const std::string& value);
    void remove(rocksdb::WriteBatch& batch, const std::string& cf, const std::string& key);
    bool commit(rocksdb::WriteBatch& batch);

    std::unique_ptr<rocksdb::Iterator> newIterator(const std::string& cf);

    bool flushSync();

    std::function<bool()> beforeCommitHook;

private:
    StorageConfig cfg_;
    std::string dir_;
    std::unique_ptr<rocksdb::DB> db_;
    std::vector<rocksdb::ColumnFamilyHandle*> handles_;
    rocksdb::Options options_;
};

}  // namespace voterpool
