#pragma once

#include "domain/Vote.h"
#include "storage/RocksDBWrapper.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace voterpool {

class AuditLogRepository {
public:
    explicit AuditLogRepository(RocksDBWrapper& db) : db_(db), salt_(makeBootSalt(this)) {}

    void append(rocksdb::WriteBatch& batch, const std::string& orgId, const AuditEvent& event);
    std::vector<AuditEvent> listByOrg(const std::string& orgId);

private:
    static std::uint64_t makeBootSalt(const void* instance);

    RocksDBWrapper& db_;
    std::uint64_t salt_;
    std::atomic<std::int64_t> seq_{0};
};

}  // namespace voterpool
