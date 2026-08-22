#pragma once

#include "core/IClock.h"
#include "domain/Proposal.h"
#include "storage/Codec.h"
#include "storage/RocksDBWrapper.h"

#include <optional>
#include <string>
#include <vector>

namespace voterpool {

class ProposalRepository {
public:
    explicit ProposalRepository(RocksDBWrapper& db, IClock& clock) : db_(db), clock_(clock) {}

    void put(rocksdb::WriteBatch& batch, const Proposal& p);
    bool put(const Proposal& p);
    std::optional<Proposal> get(const std::string& orgId, const std::string& proposalId);
    void remove(rocksdb::WriteBatch& batch, const std::string& orgId, const std::string& proposalId);
    std::vector<Proposal> listByOrg(const std::string& orgId);
    std::vector<Proposal> listByOrgAll();

    void addActiveIndex(rocksdb::WriteBatch& batch, const Proposal& p);
    void removeActiveIndex(rocksdb::WriteBatch& batch, const Proposal& p);
    void addLookup(rocksdb::WriteBatch& batch, const Proposal& p);
    std::string lookupOrg(const std::string& proposalId);
    std::int64_t countActiveGauge();

private:
    RocksDBWrapper& db_;
    IClock& clock_;
};

}  // namespace voterpool
