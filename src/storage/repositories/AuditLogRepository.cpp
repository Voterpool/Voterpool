#include "storage/repositories/AuditLogRepository.h"

#include "storage/Codec.h"
#include "storage/Keys.h"

#include <chrono>
#include <cstdint>
#include <random>

namespace voterpool {

std::uint64_t AuditLogRepository::makeBootSalt(const void* instance) {
    std::uint64_t s = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    s ^= static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(instance)) * 0x9E3779B97F4A7C15ULL;
    std::random_device rd;
    s ^= (static_cast<std::uint64_t>(rd()) << 32) ^ static_cast<std::uint64_t>(rd());
    s ^= s >> 33;
    s *= 0xff51afd7ed558ccdULL;
    s ^= s >> 33;
    s *= 0xc4ceb9fe1a85ec53ULL;
    s ^= s >> 33;
    return s;
}

void AuditLogRepository::append(rocksdb::WriteBatch& batch, const std::string& orgId, const AuditEvent& event) {
    std::int64_t seq = seq_.fetch_add(1, std::memory_order_relaxed);
    db_.put(batch, "cf_audit_log", Keys::auditKey(orgId.empty() ? "_" : orgId, event.created_at, salt_, seq),
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
