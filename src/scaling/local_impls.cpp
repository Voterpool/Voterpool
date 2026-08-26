#include "scaling/local_impls.h"

#include "server/NativeAuthProvider.h"

#include <drogon/drogon.h>

namespace voterpool::scaling {

std::set<std::string> LocalDirectory::matchName(const std::string& nameLowered) {
    return names_.matchQuery(nameLowered);
}

std::optional<std::string> LocalDirectory::findActiveByName(const std::string& nameLowered) {
    return names_.findActiveByName(nameLowered);
}

void LocalDirectory::indexOrg(const std::string& orgId, const std::string& nameLowered) {
    names_.add(orgId, nameLowered);
}

void LocalDirectory::renameOrg(const std::string& orgId, const std::string& newNameLowered) {
    names_.rename(orgId, newNameLowered);
}

void LocalDirectory::eraseOrg(const std::string& orgId) {
    names_.erase(orgId);
}

std::set<std::string> LocalDirectory::scanTag(const std::string& tagLowered) {
    return indexes_.scanTag(tagLowered);
}

std::set<std::string> LocalDirectory::scanCategory(const std::string& categoryLowered) {
    return indexes_.scanCategory(categoryLowered);
}

std::vector<std::string> LocalDirectory::scanFeedActive() {
    return indexes_.scanFeedActive();
}

std::optional<std::string> LocalDirectory::resolveProposal(const std::string& proposalId) {
    const std::string orgId = proposals_.lookupOrg(proposalId);
    if (orgId.empty()) return std::nullopt;
    return orgId;
}

LocalIdentity::LocalIdentity(AgentRepository& agents, OrgRepository& orgs)
    : agents_(agents), orgs_(orgs), tokenProvider_(std::make_unique<NativeAuthProvider>(agents)) {}

std::optional<AgentContext> LocalIdentity::resolveToken(const std::string& token) {
    return tokenProvider_->validate(token);
}

bool LocalIdentity::createAgent(const std::string& agentId, const std::string& name,
                                const std::string& apiKeyHash, std::string* outErr) {
    return agents_.create(agentId, name, apiKeyHash, outErr);
}

std::optional<Agent> LocalIdentity::getProfile(const std::string& agentId) {
    return agents_.get(agentId);
}

bool LocalIdentity::putProfile(const Agent& agent) {
    return agents_.put(agent);
}

std::int64_t LocalIdentity::countAgents() {
    return agents_.count();
}

std::vector<Membership> LocalIdentity::listOrgsOfAgent(const std::string& agentId) {
    return orgs_.listOrgsOfAgent(agentId);
}

void LocalIdentity::recordMembershipLink(rocksdb::WriteBatch& batch, const Membership& m) {
    // putMembership пишет membership и agent_orgs одним батчем — пара атомарна.
    orgs_.putMembership(batch, m);
}

void LocalIdentity::removeMembershipLink(rocksdb::WriteBatch& batch, const std::string& orgId,
                                         const std::string& agentId) {
    orgs_.deleteMembership(batch, orgId, agentId);
}

void LocalEventBus::subscribeAllOrgs(const std::vector<std::string>& orgIds,
                                     const std::string& agentId,
                                     drogon::ResponseStreamPtr stream) {
    hub_.registerStreams(orgIds, agentId, std::move(stream));
}

void LocalEventBus::registerKeepAlive(drogon::ResponseStreamPtr stream) {
    hub_.registerKeepAlive(std::move(stream));
}

void LocalEventBus::deliver(const SseEvent& event) {
    hub_.deliver(event);
}

void LocalEventBus::heartbeat() {
    hub_.heartbeat();
}

void LocalEventBus::shutdownAll() {
    hub_.shutdownAll();
}

std::size_t LocalEventBus::connectionCount() {
    return hub_.connectionCount();
}

}  // namespace voterpool::scaling
