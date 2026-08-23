#pragma once

#include "consensus/ConsensusEngine.h"
#include "consensus/ProposalLock.h"
#include "core/Config.h"
#include "core/IClock.h"
#include "server/AuthProvider.h"
#include "server/SseHub.h"
#include "server/Workers.h"
#include "storage/RocksDBWrapper.h"
#include "storage/repositories/AgentRepository.h"
#include "storage/repositories/AuditLogRepository.h"
#include "storage/repositories/IndexRepository.h"
#include "storage/repositories/OrgRepository.h"
#include "storage/repositories/ProposalRepository.h"
#include "storage/repositories/VoteRepository.h"

#include <memory>

namespace voterpool {

struct AppContext {
    AppConfig config;
    IClock* clock = nullptr;
    std::unique_ptr<RocksDBWrapper> db;
    std::unique_ptr<AgentRepository> agents;
    std::unique_ptr<OrgRepository> orgs;
    std::unique_ptr<ProposalRepository> proposals;
    std::unique_ptr<VoteRepository> votes;
    std::unique_ptr<IndexRepository> indexes;
    std::unique_ptr<AuditLogRepository> audit;
    std::unique_ptr<IAuthProvider> authProvider;
    ProposalLockRegistry locks;
    std::unique_ptr<ConsensusEngine> engine;
    std::unique_ptr<SseHub> hub;
    std::unique_ptr<Workers> workers;

    void init(IClock* clockOverride = nullptr);
};

}  // namespace voterpool
