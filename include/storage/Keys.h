#pragma once

#include "scaling/BucketResolver.h"

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

// Раскладка keyspaces v2 (docs/01, docs/16 §3.2, change add-bucket-keyspace):
//
//   ОРГ-ПЛОСКОСТЬ (владелец — бакет организации):
//     b{NNN}:org:{id}
//     b{NNN}:org:{id}:member:{agent}
//     b{NNN}:org:{id}:proposal:{pid}[:vote:{agent}]
//     b{NNN}:audit:{org}:*
//     b{NNN}:pending:{org}:{agent}
//     b{NNN}:join_limit:{org}:{yyyymmdd}
//     b{NNN}:tag:{tag}:{org}
//     b{NNN}:category:{cat}:{org}
//     b{NNN}:org_feed:{status}:{rev}:{org}
//     b{NNN}:active_proposals:{expires}:{pid}
//     b{NNN}:agent_orgs:{agent}:{org}          (бакет ОРГАНИЗАЦИИ)
//
//   СИСТЕМНАЯ ПЛОСКОСТЬ (без префикса, маршрутизационная; на Этапе 3
//   реплицируется на все узлы — docs/16 §4 SH-2):
//     agent:{id}, auth:{hash}, proposal_lookup:{pid}, meta:*
//
// Префикс вшит в сами конструкторы: call-site не может забыть scope.
// Сканы без указания организации (лента, теги, категории, TTL-индекс,
// «мои организации») ОБЯЗАНЫ обходить все kBucketCount префиксов —
// см. мульти-бакетные сканы в IndexRepository/OrgRepository/ConsensusEngine.
// Инвентаризационный тест (test_keys_bucket) следит за полнотой покрытия.

namespace voterpool::Keys {

using scaling::bucketFor;
using scaling::kBucketCount;
using scaling::scoped;

inline std::string agent(const std::string& id) { return "agent:" + id; }
inline std::string auth(const std::string& hash) { return "auth:" + hash; }
// СИСТЕМНАЯ ПЛОСКОСТЬ: резолв proposal→org для cast_vote (без префикса).
inline std::string proposalLookup(const std::string& proposalId) {
    return "proposal_lookup:" + proposalId;
}
inline std::string org(const std::string& id) { return scoped(bucketFor(id), "org:" + id); }
inline std::string membership(const std::string& orgId, const std::string& agentId) {
    return scoped(bucketFor(orgId), "org:" + orgId + ":member:" + agentId);
}
inline std::string membershipPrefix(const std::string& orgId) {
    return scoped(bucketFor(orgId), "org:" + orgId + ":member:");
}
inline std::string proposal(const std::string& orgId, const std::string& proposalId) {
    return scoped(bucketFor(orgId), "org:" + orgId + ":proposal:" + proposalId);
}
inline std::string proposalPrefix(const std::string& orgId) {
    return scoped(bucketFor(orgId), "org:" + orgId + ":proposal:");
}
inline std::string vote(const std::string& orgId, const std::string& proposalId,
                        const std::string& agentId) {
    return scoped(bucketFor(orgId),
                  "org:" + orgId + ":proposal:" + proposalId + ":vote:" + agentId);
}
inline std::string agentOrgs(const std::string& agentId, const std::string& orgId) {
    // Размещается в БАКЕТЕ ОРГАНИЗАЦИИ: запись происходит в момент join,
    // когда бакет известен (docs/16 §3.2).
    return scoped(bucketFor(orgId), "agent_orgs:" + agentId + ":" + orgId);
}

// Префикс пер-бакетного скана связей агента (listOrgsOfAgent обходит все K).
inline std::string agentOrgsPrefixIn(int bucket, const std::string& agentId) {
    return scoped(bucket, "agent_orgs:" + agentId + ":");
}

inline std::string auditKey(const std::string& orgId, std::int64_t tsMs, std::uint64_t salt,
                            std::int64_t seq) {
    char tail[64];
    snprintf(tail, sizeof(tail), "%020lld:%016llx%019lld", static_cast<long long>(tsMs),
             static_cast<unsigned long long>(salt), static_cast<long long>(seq));
    return scoped(bucketFor(orgId), "audit:" + orgId + ":" + tail);
}
inline std::string auditPrefix(const std::string& orgId) {
    return scoped(bucketFor(orgId), "audit:" + orgId + ":");
}

inline std::string pending(const std::string& orgId, const std::string& agentId) {
    return scoped(bucketFor(orgId), "pending:" + orgId + ":" + agentId);
}
inline std::string pendingPrefix(const std::string& orgId) {
    return scoped(bucketFor(orgId), "pending:" + orgId + ":");
}

inline std::string padTs(std::int64_t ts) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%019lld", static_cast<long long>(ts));
    return buf;
}

inline std::string activeProposal(const std::string& orgId, std::int64_t expiresAt,
                                  const std::string& proposalId) {
    return scoped(bucketFor(orgId),
                  "active_proposals:" + padTs(expiresAt) + ":" + proposalId);
}
// Пер-бакетный префикс для TTL-скана (closeExpired обходит все K).
inline std::string activeProposalPrefixIn(int bucket) {
    return scoped(bucket, "active_proposals:");
}
inline std::string activeProposalEntryIn(int bucket, const std::string& proposalId) {
    // Позиция поиска «хвостовых» записей предложения при зачистке:
    // минимальный expires (19 нулей) + разделитель.
    return scoped(bucket, "active_proposals:" + std::string(19, '0') + ":" + proposalId);
}

inline std::int64_t reverseTs(std::int64_t createdSec) { return 253402300799LL - createdSec; }

inline std::string orgFeed(const std::string& status, std::int64_t createdSec,
                           const std::string& orgId) {
    return scoped(bucketFor(orgId),
                  "org_feed:" + status + ":" + padTs(reverseTs(createdSec)) + ":" + orgId);
}
// Пер-бакетный префикс ленты ACTIVE (scanFeedActive обходит все K и
// восстанавливает глобальный порядок по rev-таймстампу).
inline std::string orgFeedActivePrefixIn(int bucket) {
    return scoped(bucket, "org_feed:ACTIVE:");
}

inline std::string tagLower(std::string tag) {
    for (auto& c : tag) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return tag;
}
inline std::string tag(const std::string& tagLowered, const std::string& orgId) {
    return scoped(bucketFor(orgId), "tag:" + tagLowered + ":" + orgId);
}
// Пер-бакетный префикс скана тега.
inline std::string tagPrefixIn(int bucket, const std::string& tagLowered) {
    return scoped(bucket, "tag:" + tagLowered + ":");
}
inline std::string nameLower(std::string name) {
    for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return name;
}
inline std::string category(const std::string& categoryLowered, const std::string& orgId) {
    return scoped(bucketFor(orgId), "category:" + categoryLowered + ":" + orgId);
}
inline std::string categoryPrefixIn(int bucket, const std::string& categoryLowered) {
    return scoped(bucket, "category:" + categoryLowered + ":");
}

inline std::string yyyymmdd(std::int64_t epochSec) {
    std::time_t t = static_cast<std::time_t>(epochSec);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[16];
    strftime(buf, sizeof(buf), "%Y%m%d", &tm);
    return buf;
}

inline std::string joinLimit(const std::string& orgId, std::int64_t epochSec) {
    return scoped(bucketFor(orgId), "join_limit:" + orgId + ":" + yyyymmdd(epochSec));
}

}  // namespace voterpool::Keys
