#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace voterpool {

class RocksDBWrapper;

inline constexpr int VOTERPOOL_SCHEMA_VERSION = 2;
inline constexpr const char* kSchemaVersionKey = "meta:schema_version";

enum class SchemaGateResult { kInitialized, kUpToDate, kMigrated, kFatalNewerSchema, kError };

class SchemaManager {
public:
    explicit SchemaManager(RocksDBWrapper& db);

    SchemaGateResult run();

    int dbVersion() const { return dbVersion_; }
    int targetVersion() const { return VOTERPOOL_SCHEMA_VERSION; }

private:
    bool readVersion(int& out) const;
    bool writeVersion(int v);
    bool migrateTo(int from, int to);

    RocksDBWrapper& db_;
    int dbVersion_ = 0;
};

}  // namespace voterpool
