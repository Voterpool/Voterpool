#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

namespace voterpool::Keys {

inline std::string agent(const std::string& id) { return "agent:" + id; }
inline std::string auth(const std::string& hash) { return "auth:" + hash; }
inline std::string org(const std::string& id) { return "org:" + id; }
inline std::string membership(const std::string& orgId, const std::string& agentId) {
    return "org:" + orgId + ":member:" + agentId;
}
inline std::string membershipPrefix(const std::string& orgId) { return "org:" + orgId + ":member:"; }
inline std::string proposal(const std::string& orgId, const std::string& proposalId) {
    return "org:" + orgId + ":proposal:" + proposalId;
}
inline std::string proposalPrefix(const std::string& orgId) { return "org:" + orgId + ":proposal:"; }
inline std::string vote(const std::string& orgId, const std::string& proposalId, const std::string& agentId) {
    return "org:" + orgId + ":proposal:" + proposalId + ":vote:" + agentId;
}
inline std::string agentOrgs(const std::string& agentId, const std::string& orgId) {
    return "agent_orgs:" + agentId + ":" + orgId;
}
inline std::string agentOrgsPrefix(const std::string& agentId) { return "agent_orgs:" + agentId + ":"; }

inline std::string auditKey(const std::string& orgId, std::int64_t tsMs, std::uint64_t salt,
                            std::int64_t seq) {
    char tail[64];
    snprintf(tail, sizeof(tail), "%020lld:%016llx%019lld", static_cast<long long>(tsMs),
             static_cast<unsigned long long>(salt), static_cast<long long>(seq));
    return "audit:" + orgId + ":" + tail;
}
inline std::string auditPrefix(const std::string& orgId) { return "audit:" + orgId + ":"; }

inline std::string pending(const std::string& orgId, const std::string& agentId) {
    return "pending:" + orgId + ":" + agentId;
}
inline std::string pendingPrefix(const std::string& orgId) { return "pending:" + orgId + ":"; }

inline std::string padTs(std::int64_t ts) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%019lld", static_cast<long long>(ts));
    return buf;
}

inline std::string activeProposal(std::int64_t expiresAt, const std::string& proposalId) {
    return "active_proposals:" + padTs(expiresAt) + ":" + proposalId;
}
inline constexpr const char* activeProposalPrefix() { return "active_proposals:"; }

inline std::string proposalLookup(const std::string& proposalId) { return "proposal_lookup:" + proposalId; }

inline std::int64_t reverseTs(std::int64_t createdSec) { return 253402300799LL - createdSec; }

inline std::string orgFeed(const std::string& status, std::int64_t createdSec, const std::string& orgId) {
    return "org_feed:" + status + ":" + padTs(reverseTs(createdSec)) + ":" + orgId;
}
inline std::string orgFeedPrefixActive() { return "org_feed:ACTIVE:"; }

inline std::string tagLower(std::string tag) {
    for (auto& c : tag) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return tag;
}
inline std::string tag(const std::string& tagLowered, const std::string& orgId) {
    return "tag:" + tagLowered + ":" + orgId;
}
inline std::string nameLower(std::string name) {
    for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return name;
}
inline std::string category(const std::string& categoryLowered, const std::string& orgId) {
    return "category:" + categoryLowered + ":" + orgId;
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
    return "join_limit:" + orgId + ":" + yyyymmdd(epochSec);
}

}  // namespace voterpool::Keys
