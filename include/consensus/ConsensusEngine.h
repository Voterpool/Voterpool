#pragma once

#include "consensus/ProposalLock.h"
#include "core/IClock.h"
#include "core/Result.h"
#include "domain/Enums.h"
#include "domain/Vote.h"

#include <functional>
#include <vector>

namespace rocksdb {
class WriteBatch;
}

namespace voterpool {

class Proposal;

struct VoteReceipt {
    std::string proposal_id;
    std::string decision;
    double power_applied = 0.0;
    ProposalStatus proposal_status = ProposalStatus::ACTIVE;
    double current_yes_power = 0.0;
    double current_no_power = 0.0;
};

struct DissolveOutcome {
    std::int64_t closedCount = 0;
    std::vector<SseEvent> events;
};

class ConsensusEngine {
public:
    struct Deps {
        class RocksDBWrapper* db = nullptr;
        class OrgRepository* orgs = nullptr;
        class ProposalRepository* proposals = nullptr;
        class VoteRepository* votes = nullptr;
        class IndexRepository* indexes = nullptr;
        class AuditLogRepository* audit = nullptr;
        KeyedMutexRegistry* locks = nullptr;
        KeyedMutexRegistry* orgLocks = nullptr;
        IClock* clock = nullptr;
        class OrgNameRegistry* orgNames = nullptr;
        std::function<void(const SseEvent&)> emit;
    };

    explicit ConsensusEngine(Deps d) : d_(d) {}

    Result<VoteReceipt> castVote(const std::string& agentId, const std::string& proposalId, VoteDecision decision);
    void closeExpired(std::int64_t nowSec, size_t maxBatch = 1000);
    bool closeProposalByTimer(const std::string& orgId, const std::string& proposalId);
    Result<DissolveOutcome> dissolveOrganization(const std::string& orgId, const std::string& dissolvedBy);

private:
    struct ClosedInfo {
        bool closed = false;
        ProposalStatus status = ProposalStatus::ACTIVE;
        bool actionApplied = false;
        std::string actionKind;
        bool configDeltaApplied = false;
    };

    void finalizeLocked(rocksdb::WriteBatch& batch, Proposal& p, ProposalStatus finalStatus, ClosedInfo& info);
    void emitClosed(const Proposal& p, const ClosedInfo& info);

    Deps d_;
};

}  // namespace voterpool
