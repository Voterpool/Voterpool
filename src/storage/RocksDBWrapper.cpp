#include "storage/RocksDBWrapper.h"

#include "core/Metrics.h"

#include <rocksdb/statistics.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>

namespace voterpool {

namespace {

struct RocksdbTickerExport {
    rocksdb::Tickers ticker;
    const char* metric;
};

const RocksdbTickerExport kRocksdbExports[] = {
    {rocksdb::BLOCK_CACHE_HIT, "voterpool_rocksdb_block_cache_hits_total"},
    {rocksdb::BLOCK_CACHE_MISS, "voterpool_rocksdb_block_cache_misses_total"},
    {rocksdb::WAL_FILE_SYNCED, "voterpool_rocksdb_wal_synced_total"},
    {rocksdb::FLUSH_WRITE_BYTES, "voterpool_rocksdb_flush_write_bytes_total"},
    {rocksdb::COMPACT_READ_BYTES, "voterpool_rocksdb_compaction_read_bytes_total"},
    {rocksdb::COMPACT_WRITE_BYTES, "voterpool_rocksdb_compaction_write_bytes_total"},
};

struct RocksdbPropertyExport {
    std::string property;  // имя свойства для DB::GetIntProperty (Slice)
    const char* metric;
};

const RocksdbPropertyExport kRocksdbPropertyExports[] = {
    {rocksdb::DB::Properties::kBlockCacheUsage, "voterpool_rocksdb_block_cache_usage"},
    {rocksdb::DB::Properties::kBlockCacheCapacity, "voterpool_rocksdb_block_cache_capacity"},
    {rocksdb::DB::Properties::kEstimatePendingCompactionBytes,
     "voterpool_rocksdb_estimate_pending_compaction_bytes"},
};

}  // namespace

RocksDBWrapper::RocksDBWrapper(const StorageConfig& cfg, const std::string& dirOverride)
    : cfg_(cfg), dir_(dirOverride.empty() ? cfg.path : dirOverride) {}

RocksDBWrapper::~RocksDBWrapper() { close(); }

void RocksDBWrapper::publishStatisticsToRegistry() {
    if (!db_ || !options_.statistics) return;
    rocksdb::Statistics* stats = options_.statistics.get();
    MetricsRegistry& registry = MetricsRegistry::instance();
    for (const auto& exportEntry : kRocksdbExports) {
        registry.setGauge(exportEntry.metric, {},
                          static_cast<std::int64_t>(stats->getTickerCount(exportEntry.ticker)));
    }
    for (const auto& exportEntry : kRocksdbPropertyExports) {
        std::uint64_t value = 0;
        if (db_->GetIntProperty(exportEntry.property, &value)) {
            registry.setGauge(exportEntry.metric, {}, static_cast<std::int64_t>(value));
        }
    }}

bool RocksDBWrapper::open() {
    rocksdb::Options opts;
    opts.create_if_missing = true;
    opts.max_open_files = cfg_.max_open_files;
    opts.write_buffer_size = cfg_.write_buffer_size;
    opts.max_write_buffer_number = cfg_.max_write_buffer_number;
    if (cfg_.log_level == "DEBUG") opts.info_log_level = rocksdb::InfoLogLevel::DEBUG_LEVEL;
    else if (cfg_.log_level == "INFO") opts.info_log_level = rocksdb::InfoLogLevel::INFO_LEVEL;
    else if (cfg_.log_level == "ERROR") opts.info_log_level = rocksdb::InfoLogLevel::ERROR_LEVEL;
    else opts.info_log_level = rocksdb::InfoLogLevel::WARN_LEVEL;

    opts.statistics = rocksdb::CreateDBStatistics();
    options_ = opts;

    std::vector<std::string> existing;
    rocksdb::Status listStatus = rocksdb::DB::ListColumnFamilies(opts, dir_, &existing);
    bool fresh = existing.empty();

    std::vector<rocksdb::ColumnFamilyDescriptor> descriptors;
    if (!fresh) {
        for (const char* name : kCfNames) {
            for (const auto& e : existing) {
                if (e == name) {
                    descriptors.emplace_back(name, rocksdb::ColumnFamilyOptions());
                    break;
                }
            }
        }
    } else {
        descriptors.emplace_back("default", rocksdb::ColumnFamilyOptions());
    }

    std::vector<rocksdb::ColumnFamilyHandle*> opened;
    rocksdb::DB* rawDb = nullptr;
    rocksdb::Status st = rocksdb::DB::Open(options_, dir_, descriptors, &opened, &rawDb);
    if (!st.ok()) {
        if (st.IsCorruption() || st.IsIOError()) DbHealth::instance().markUnhealthy(st.ToString());
        spdlog::critical("Failed to open RocksDB at {}: {}", dir_, st.ToString());
        return false;
    }
    db_.reset(rawDb);

    handles_.assign(sizeof(kCfNames) / sizeof(kCfNames[0]), nullptr);
    for (auto* h : opened) {
        for (size_t i = 0; i < sizeof(kCfNames) / sizeof(kCfNames[0]); ++i) {
            if (h->GetName() == kCfNames[i]) handles_[i] = h;
        }
    }
    bool createdAll = true;
    for (size_t i = 1; i < sizeof(kCfNames) / sizeof(kCfNames[0]); ++i) {
        if (handles_[i] == nullptr) {
            rocksdb::ColumnFamilyHandle* h = nullptr;
            st = db_->CreateColumnFamily(rocksdb::ColumnFamilyOptions(), kCfNames[i], &h);
            if (!st.ok()) {
                createdAll = false;
                break;
            }
            handles_[i] = h;
        }
    }
    if (!createdAll || handles_[0] == nullptr) {
        spdlog::critical("Cannot create column families in {}", dir_);
        return false;
    }
    MetricsRegistry::instance().setGauge("voterpool_db_healthy", {}, 1);
    return true;
}

void RocksDBWrapper::close() {
    if (!db_) return;
    db_->Flush(rocksdb::FlushOptions());
    for (auto* h : handles_) db_->DestroyColumnFamilyHandle(h);
    handles_.clear();
    db_->Close();
    db_.reset();
}

rocksdb::ColumnFamilyHandle* RocksDBWrapper::cf(const std::string& name) const {
    for (size_t i = 0; i < sizeof(kCfNames) / sizeof(kCfNames[0]); ++i) {
        if (kCfNames[i] == name && handles_[i]) return handles_[i];
    }
    return db_->DefaultColumnFamily();
}

std::optional<std::string> RocksDBWrapper::get(const std::string& cfName, const std::string& key) {
    std::string value;
    rocksdb::Status st = db_->Get(rocksdb::ReadOptions(), cf(cfName), key, &value);
    if (st.ok()) return value;
    return std::nullopt;
}

bool RocksDBWrapper::put(rocksdb::WriteBatch& batch, const std::string& cfName, const std::string& key, const std::string& value) {
    return batch.Put(cf(cfName), key, value).ok();
}

void RocksDBWrapper::remove(rocksdb::WriteBatch& batch, const std::string& cfName, const std::string& key) {
    batch.Delete(cf(cfName), key);
}

bool RocksDBWrapper::commit(rocksdb::WriteBatch& batch) {
    if (beforeCommitHook && !beforeCommitHook()) {
        MetricsRegistry::instance().incCounter("voterpool_db_write_failures_total", {{"kind", "io"}});
        DbHealth::instance().markUnhealthy("simulated IO failure");
        spdlog::error("RocksDB write failed: simulated IO error");
        return false;
    }
    rocksdb::WriteOptions wo;
    wo.sync = true;
    rocksdb::Status st = db_->Write(wo, &batch);
    if (!st.ok()) {
        if (st.IsIOError() || st.IsCorruption()) {
            MetricsRegistry::instance().incCounter("voterpool_db_write_failures_total", {{"kind", st.IsCorruption() ? "corruption" : "io"}});
            DbHealth::instance().markUnhealthy(st.ToString());
        }
        spdlog::error("RocksDB write failed: {}", st.ToString());
        return false;
    }
    return true;
}

std::unique_ptr<rocksdb::Iterator> RocksDBWrapper::newIterator(const std::string& cfName) {
    return std::unique_ptr<rocksdb::Iterator>(db_->NewIterator(rocksdb::ReadOptions(), cf(cfName)));
}

bool RocksDBWrapper::flushSync() {
    if (!db_) return false;
    rocksdb::FlushOptions fo;
    fo.wait = true;
    rocksdb::Status st = db_->Flush(fo);
    if (!st.ok()) {
        if (st.IsIOError() || st.IsCorruption()) DbHealth::instance().markUnhealthy(st.ToString());
        return false;
    }
    return true;
}

}  // namespace voterpool
