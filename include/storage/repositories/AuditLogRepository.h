#pragma once

#include "domain/Vote.h"
#include "storage/RocksDBWrapper.h"

#include <vector>

namespace voterpool {

class AuditLogRepository {
public:
    explicit AuditLogRepository(RocksDBWrapper& db) : db_(db) {}

    void append(rocksdb::WriteBatch& batch, const std::string& orgId, const AuditEvent& event);
    std::vector<AuditEvent> listByOrg(const std::string& orgId);

private:
    RocksDBWrapper& db_;
};

}  // namespace voterpool
