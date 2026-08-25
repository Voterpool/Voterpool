#include "storage/SchemaVersion.h"

#include "storage/Codec.h"
#include "storage/Keys.h"

#include "core/IClock.h"
#include "core/Metrics.h"
#include "storage/RocksDBWrapper.h"

#include <rocksdb/utilities/write_batch_with_index.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <map>

namespace voterpool {

SchemaManager::SchemaManager(RocksDBWrapper& db, bool dryRun)
    : db_(db), dryRun_(dryRun) {}

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


namespace {

// Классификация ключей миграции v2→v3 (design D3, change add-bucket-keyspace).
enum class MigrateClass { Scoped, Skip, Fatal };

// Целевой ключ с префиксом бакета; orgId выводится из ключа или значения.
MigrateClass classifyKey(const std::string& cf, const std::string& k,
                         const std::string& value, std::string& dest, std::string& err) {
    using namespace Keys;
    // Идемпотентность (tasks 3.2): ключ УЖЕ с префиксом бакета — перенесён
    // ранее (докат после сбоя). Системная плоскость префикса не имеет,
    // коллизии невозможны.
    if (k.size() > 5 && k[0] == 'b' && std::isdigit(static_cast<unsigned char>(k[1])) &&
        std::isdigit(static_cast<unsigned char>(k[2])) &&
        std::isdigit(static_cast<unsigned char>(k[3])) && k[4] == ':')
        return MigrateClass::Skip;
    auto lastSegment = [](const std::string& key) {
        const size_t sep = key.rfind(':');
        return sep == std::string::npos ? std::string{} : key.substr(sep + 1);
    };
    auto secondToken = [](const std::string& prefix, const std::string& key) {
        std::string rest = key.substr(prefix.size());
        const size_t sep = rest.find(':');
        return sep == std::string::npos ? rest : rest.substr(0, sep);
    };
    // Орг-семейства: "org:{id}..." → бакет по первому токену после "org:".
    auto orgScoped = [&](const std::string& rawKey) -> MigrateClass {
        if (rawKey.rfind("org:", 0) != 0) return MigrateClass::Fatal;
        const std::string orgId = secondToken("org:", rawKey);
        if (orgId.empty()) return MigrateClass::Fatal;
        dest = scoped(bucketFor(orgId), rawKey);
        return MigrateClass::Scoped;
    };

    if (cf == "cf_organizations" || cf == "cf_memberships" || cf == "cf_proposals" ||
        cf == "cf_votes")
        return orgScoped(k);

    if (cf == "cf_agent_orgs") {
        if (k.rfind("agent_orgs:", 0) != 0) return MigrateClass::Fatal;
        // agent_orgs:{agent}:{org} → бакет ОРГАНИЗАЦИИ (последний сегмент).
        const std::string orgId = lastSegment(k);
        if (orgId.empty()) return MigrateClass::Fatal;
        dest = scoped(bucketFor(orgId), k);
        return MigrateClass::Scoped;
    }

    if (cf == "cf_indexes") {
        if (k.rfind("proposal_lookup:", 0) == 0) return MigrateClass::Skip;  // система
        if (k.rfind("active_proposals:", 0) == 0) {
            // Владелец — значение записи (org_id); пустое значение неисправимо.
            if (value.empty()) {
                err = "active_proposals entry without owner org: '" + k + "'";
                return MigrateClass::Fatal;
            }
            dest = scoped(bucketFor(value), k);
            return MigrateClass::Scoped;
        }
        if (k.rfind("tag:", 0) == 0 || k.rfind("category:", 0) == 0 ||
            k.rfind("org_feed:", 0) == 0) {
            const std::string orgId = lastSegment(k);
            if (orgId.empty()) return MigrateClass::Fatal;
            dest = scoped(bucketFor(orgId), k);
            return MigrateClass::Scoped;
        }
        if (k.rfind("join_limit:", 0) == 0 || k.rfind("pending:", 0) == 0) {
            const std::string orgId = secondToken("", k);
            if (orgId.empty()) return MigrateClass::Fatal;
            dest = scoped(bucketFor(orgId), k);
            return MigrateClass::Scoped;
        }
        err = "unclassified cf_indexes key: '" + k + "'";
        return MigrateClass::Fatal;
    }

    err = "unexpected column family for migration: '" + cf + "'";
    return MigrateClass::Fatal;
}

constexpr const char* kMigrate3Cfs[] = {"cf_organizations", "cf_memberships",
                                        "cf_proposals",     "cf_votes",
                                        "cf_indexes",       "cf_agent_orgs"};

}  // namespace

bool SchemaManager::migrateTo(int from, int to) {
    spdlog::info("Migrating schema v{} -> v{} ...", from, to);
    std::int64_t records = 0;
    if (from == 2 && to == 3) {
        if (!migrateTo3(records)) return false;
        if (dryRun_) return true;  // версия и данные не трогаются (tasks 3.2)
        if (!writeVersion(to)) return false;
        MetricsRegistry::instance().incCounter(
            "voterpool_schema_migration_records_total",
            {{"from", std::to_string(from)}, {"to", std::to_string(to)}}, records);
        spdlog::info("Migrating schema v{} -> v{} done ({} records)", from, to, records);
        return true;
    }
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


// ---- Миграция v2→v3: префикс логического бакета b{NNN}: для орг-плоскости ----
// Design D3 (change add-bucket-keyspace): валидация ДО мутаций; батчи по 10k;
// чекпоинт-маркер в том же WriteBatch → докат после падения без дублей и потерь;
// dry-run считает и репортит, ничего не пишет.
bool SchemaManager::migrateTo3(std::int64_t& records) {
    const std::string markerKey = kMigration3CheckpointKey;

    // --- Проход 1 (validate): каждый ключ классифицируем; битое — фатально.
    auto classifyAll = [&](bool countOnly) -> bool {
        std::map<std::string, std::int64_t> perCf;
        for (const char* cf : kMigrate3Cfs) {
            auto it = db_.newIterator(cf);
            for (it->SeekToFirst(); it->Valid(); it->Next()) {
                std::string dest, err;
                auto cls =
                    classifyKey(cf, it->key().ToString(), it->value().ToString(), dest, err);
                if (cls == MigrateClass::Fatal) {
                    spdlog::critical(
                        "Schema migration v2 -> v3 aborted: {}; restore from checkpoint "
                        "or remove the key, then restart",
                        err);
                    return false;
                }
                if (cls != MigrateClass::Scoped) continue;  // система — не переносится
                if (countOnly) ++perCf[cf];                 // мутация ниже при !countOnly
            }
        }
        if (countOnly) {
            for (const char* cf : kMigrate3Cfs)
                dryRunReport_.emplace_back(cf, perCf[cf]);
        }
        return true;
    };
    if (!classifyAll(dryRun_)) return false;
    if (dryRun_) {
        std::int64_t total = 0;
        for (const auto& [cf, n] : dryRunReport_) {
            spdlog::info("[dry-run] {}: {} records would be re-prefixed", cf, n);
            total += n;
        }
        spdlog::info("[dry-run] total {} records; database NOT modified", total);
        return true;
    }

    // --- Проход 2 (mutate): позиция возобновления из чекпоинта.
    std::string resumeCf, resumeKey;
    if (auto last = db_.get("default", markerKey)) {
        const size_t sep = last->find('\x1f');
        if (sep != std::string::npos) {
            resumeCf = last->substr(0, sep);
            resumeKey = last->substr(sep + 1);
        }
        spdlog::info("Resuming v2 -> v3 migration after checkpoint '{}:{}'", resumeCf, resumeKey);
    }
    // Если маркера нет или он указывает на несуществующий CF — старт с начала.
    size_t startIdx = 0;
    if (!resumeCf.empty() || !resumeKey.empty()) {
        for (size_t i = 0; i < sizeof(kMigrate3Cfs) / sizeof(kMigrate3Cfs[0]); ++i) {
            if (resumeCf == kMigrate3Cfs[i]) startIdx = i;
        }
        if (resumeKey.empty()) startIdx = 0;  // маркер без ключа не бывает
    }

    rocksdb::WriteBatch batch;
    int batched = 0;
    bool sawResumeCf = resumeCf.empty();  // до резюме-CF всё уже перенесено
    std::string lastMovedCf, lastMovedKey;

    auto flushBatch = [&](bool finalFlush) -> bool {
        if (batched == 0) return true;
        if (!lastMovedCf.empty())
            db_.put(batch, "default", markerKey, lastMovedCf + '\x1f' + lastMovedKey);
        if (finalFlush) db_.remove(batch, "default", markerKey);
        if (!db_.commit(batch)) return false;
        batch.Clear();
        batched = 0;
        return true;
    };

    for (size_t ci = 0; ci < sizeof(kMigrate3Cfs) / sizeof(kMigrate3Cfs[0]); ++ci) {
        const std::string cf = kMigrate3Cfs[ci];
        if (!sawResumeCf) {
            if (cf != resumeCf) continue;  // CF до чекпоинта полностью готов
            sawResumeCf = true;
        }
        auto it = db_.newIterator(cf);
        if (!resumeKey.empty() && cf == resumeCf) {
            it->Seek(resumeKey);
            // Пропускаем сам чекпоинт-ключ (он уже перенесён в прошлом батче).
            if (it->Valid() && it->key().ToString() == resumeKey) it->Next();
            resumeKey.clear();
        } else {
            it->SeekToFirst();
        }
        for (; it->Valid(); it->Next()) {
            const std::string src = it->key().ToString();
            const std::string value = it->value().ToString();
            std::string dest, err;
            auto cls = classifyKey(cf, src, value, dest, err);
            if (cls != MigrateClass::Scoped) continue;  // Skip: система; Fatal тут невозможен
            db_.put(batch, cf, dest, value);
            if (dest != src)
                db_.remove(batch, cf, src);  // повторный проход: dest==src → только Put
            lastMovedCf = cf;
            lastMovedKey = src;
            ++batched;
            ++records;
            if (batched >= 10000 && !flushBatch(false)) return false;
        }
    }
    if (!flushBatch(true)) return false;
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
