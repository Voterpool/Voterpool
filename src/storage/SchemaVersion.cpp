#include "storage/SchemaVersion.h"

#include "storage/Codec.h"
#include "storage/Keys.h"

#include "core/IClock.h"
#include "core/Metrics.h"
#include "storage/RocksDBWrapper.h"

#include <rocksdb/utilities/write_batch_with_index.h>

#include <spdlog/spdlog.h>

namespace voterpool {

SchemaManager::SchemaManager(RocksDBWrapper& db) : db_(db) {}

bool SchemaManager::readVersion(int& out) const {
    auto v = db_.get("default", kSchemaVersionKey);
    if (!v) return false;
    out = std::stoi(*v);
    return true;
}

bool SchemaManager::writeVersion(int v) {
    rocksdb::WriteBatch batch;
    db_.put(batch, "default", kSchemaVersionKey, std::to_string(v));
    return db_.commit(batch);
}

bool SchemaManager::migrateTo(int from, int to) {
    spdlog::info("Migrating schema v{} -> v{} ...", from, to);
    std::int64_t records = 0;
    if (from == 1 && to == 2) {
        // Проход 1 (validate): каждая запись cf_proposals обязана быть
        // корректным JSON. Битая запись неисправима — фатально прерываем
        // миграцию до любой мутации, диск остаётся нетронутым.
        {
            auto it = db_.newIterator("cf_proposals");
            for (it->SeekToFirst(); it->Valid(); it->Next()) {
                if (!Codec::parse(it->value().ToString())) {
                    spdlog::critical(
                        "Schema migration v{} -> v{} aborted: record '{}' is not valid JSON; "
                        "restore this key from checkpoint or remove it, then restart",
                        from, to, it->key().ToString());
                    return false;
                }
            }
        }
        // Проход 2 (mutate): все записи валидны — дописываем поле старым
        // записям, неизвестные поля сохраняются.
        std::vector<std::pair<std::string, std::string>> updates;
        auto it = db_.newIterator("cf_proposals");
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            std::string value = it->value().ToString();
            auto json = Codec::parse(value);
            if (!json || !json->isMember("config_at_creation")) {
                Json::Value cfg;
                cfg["consensus_model"] = "MAJORITY";
                cfg["quorum_percentage"] = 51;
                cfg["voting_duration_sec"] = 3600;
                cfg["power_distribution"] = "EQUAL";
                (*json)["config_at_creation"] = cfg;
                updates.emplace_back(it->key().ToString(), Codec::dump(*json));
            }
            ++records;
            if (updates.size() >= 10000) {
                rocksdb::WriteBatch batch;
                for (const auto& [k, v] : updates) db_.put(batch, "cf_proposals", k, v);
                if (!db_.commit(batch)) return false;
                updates.clear();
            }
        }
        if (!updates.empty()) {
            rocksdb::WriteBatch batch;
            for (const auto& [k, v] : updates) db_.put(batch, "cf_proposals", k, v);
            if (!db_.commit(batch)) return false;
        }
    }
    if (!writeVersion(to)) return false;
    MetricsRegistry::instance().incCounter(
        "voterpool_schema_migration_records_total", {{"from", std::to_string(from)}, {"to", std::to_string(to)}}, records);
    spdlog::info("Migrating schema v{} -> v{} done ({} records)", from, to, records);
    return true;
}

SchemaGateResult SchemaManager::run() {
    int version = 0;
    if (!readVersion(version)) {
        if (!writeVersion(VOTERPOOL_SCHEMA_VERSION)) return SchemaGateResult::kError;
        dbVersion_ = VOTERPOOL_SCHEMA_VERSION;
        return SchemaGateResult::kInitialized;
    }
    dbVersion_ = version;
    if (version > VOTERPOOL_SCHEMA_VERSION) {
        spdlog::critical("Database schema v{} is newer than binary schema v{}; downgrade is not supported",
                         version, VOTERPOOL_SCHEMA_VERSION);
        return SchemaGateResult::kFatalNewerSchema;
    }
    while (version < VOTERPOOL_SCHEMA_VERSION) {
        if (!migrateTo(version, version + 1)) return SchemaGateResult::kError;
        ++version;
        dbVersion_ = version;
    }
    return version == VOTERPOOL_SCHEMA_VERSION ? SchemaGateResult::kUpToDate : SchemaGateResult::kMigrated;
}

}  // namespace voterpool
