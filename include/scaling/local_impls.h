#pragma once

#include "scaling/IIdentity.h"
#include "scaling/IDirectory.h"
#include "scaling/IEventBus.h"
#include "server/AuthProvider.h"
#include "server/SseHub.h"
#include "storage/OrgNameRegistry.h"
#include "storage/repositories/AgentRepository.h"
#include "storage/repositories/IndexRepository.h"
#include "storage/repositories/OrgRepository.h"
#include "storage/repositories/ProposalRepository.h"

namespace voterpool::scaling {

// Локальные имплементации портов поверх существующих репозиториев:
// семантика байт-в-байт эквивалентна прежнему прямому коду.

class LocalDirectory : public IDirectory {
public:
    LocalDirectory(OrgNameRegistry& names, IndexRepository& indexes, OrgRepository& orgs,
                   ProposalRepository& proposals)
        : names_(names), indexes_(indexes), orgs_(orgs), proposals_(proposals) {}

    std::set<std::string> matchName(const std::string& nameLowered) override;
    std::optional<std::string> findActiveByName(const std::string& nameLowered) override;
    void indexOrg(const std::string& orgId, const std::string& nameLowered) override;
    void renameOrg(const std::string& orgId, const std::string& newNameLowered) override;
    void eraseOrg(const std::string& orgId) override;
    std::set<std::string> scanTag(const std::string& tagLowered) override;
    std::set<std::string> scanCategory(const std::string& categoryLowered) override;
    std::vector<std::string> scanFeedActive() override;
    std::optional<std::string> resolveProposal(const std::string& proposalId) override;

private:
    OrgNameRegistry& names_;
    IndexRepository& indexes_;
    OrgRepository& orgs_;       // get() при фильтрации ленты — как в текущем коде
    ProposalRepository& proposals_;  // proposal_lookup → org_id
};

class LocalIdentity : public IIdentity {
public:
    explicit LocalIdentity(AgentRepository& agents, OrgRepository& orgs);

    std::optional<AgentContext> resolveToken(const std::string& token) override;
    bool createAgent(const std::string& agentId, const std::string& name,
                     const std::string& apiKeyHash, std::string* outErr = nullptr) override;
    std::optional<Agent> getProfile(const std::string& agentId) override;
    bool putProfile(const Agent& agent) override;
    std::int64_t countAgents() override;
    std::vector<Membership> listOrgsOfAgent(const std::string& agentId) override;
    void recordMembershipLink(rocksdb::WriteBatch& batch, const Membership& m) override;
    void removeMembershipLink(rocksdb::WriteBatch& batch, const std::string& orgId,
                              const std::string& agentId) override;

private:
    AgentRepository& agents_;
    OrgRepository& orgs_;
    std::unique_ptr<IAuthProvider> tokenProvider_;  // NativeAuthProvider
};

class LocalEventBus : public IEventBus {
public:
    explicit LocalEventBus(SseHub& hub) : hub_(hub) {}

    void subscribeAllOrgs(const std::vector<std::string>& orgIds, const std::string& agentId,
                          drogon::ResponseStreamPtr stream) override;
    void deliver(const SseEvent& event) override;
    void heartbeat() override;
    void shutdownAll() override;
    std::size_t connectionCount() override;

private:
    SseHub& hub_;
};

}  // namespace voterpool::scaling
