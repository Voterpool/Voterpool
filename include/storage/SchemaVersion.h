#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace voterpool {

class RocksDBWrapper;

inline constexpr int VOTERPOOL_SCHEMA_VERSION = 3;
inline constexpr const char* kSchemaVersionKey = "meta:schema_version";
// Чекпоинт миграции v2→v3 (docs/12): позиция последнего перенесённого ключа.
inline constexpr const char* kMigration3CheckpointKey = "meta:migration:3:last";

enum class SchemaGateResult { kInitialized, kUpToDate, kMigrated, kFatalNewerSchema, kError };

class SchemaManager {
public:
    // dryRun=true (CLI --migrate-dry-run): валидация и подсчёт БЕЗ записи;
    // версия схемы не меняется, отчёт — dryRunReport() (tasks 3.2/3.4).
    explicit SchemaManager(RocksDBWrapper& db, bool dryRun = false);

    SchemaGateResult run();

    int dbVersion() const { return dbVersion_; }
    int targetVersion() const { return VOTERPOOL_SCHEMA_VERSION; }

    // Отчёт dry-run: пары <column_family, количество записей к переносу>.
    const std::vector<std::pair<std::string, std::int64_t>>& dryRunReport() const {
        return dryRunReport_;
    }

private:
    bool readVersion(int& out) const;
    bool writeVersion(int v);
    bool migrateTo(int from, int to);
    bool migrateTo3(std::int64_t& records);

    RocksDBWrapper& db_;
    bool dryRun_ = false;
    std::vector<std::pair<std::string, std::int64_t>> dryRunReport_;
    int dbVersion_ = 0;
};

}  // namespace voterpool
