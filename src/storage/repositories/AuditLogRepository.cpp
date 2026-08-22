#include "storage/repositories/AuditLogRepository.h"

#include "storage/Codec.h"
#include "storage/Keys.h"

#include "storage/Keys.h"

#include <atomic>

namespace voterpool {
namespace {
std::atomic<int> g_seq{0};
}

void AuditLogRepository::append(rocksdb::WriteBatch& batch, const std::string& orgId, const AuditEvent& event) {
    int seq = g_seq.fetch_add(1) % 1000;
    db_.put(batch, "cf_audit_log", Keys::auditKey(orgId.empty() ? "_" : orgId, event.created_at, seq),
            Codec::serializeAudit(event));
}

std::vector<AuditEvent> AuditLogRepository::listByOrg(const std::string& orgId) {
    std::vector<AuditEvent> out;
    auto it = db_.newIterator("cf_audit_log");
    for (it->Seek(Keys::auditPrefix(orgId)); it->Valid(); it->Next()) {
        std::string k = it->key().ToString();
        if (k.rfind(Keys::auditPrefix(orgId), 0) != 0) break;
        auto e = Codec::deserializeAudit(it->value().ToString());
        if (e) out.push_back(std::move(*e));
    }
    return out;
}

}  // namespace voterpool
