#pragma once

#include "core/IClock.h"
#include "domain/Vote.h"
#include "storage/Codec.h"
#include "storage/RocksDBWrapper.h"

#include <optional>
#include <string>
#include <vector>

namespace voterpool {

class VoteRepository {
public:
    explicit VoteRepository(RocksDBWrapper& db, IClock& clock) : db_(db), clock_(clock) {}

    void put(rocksdb::WriteBatch& batch, const std::string& orgId, const Vote& v);
    bool put(const std::string& orgId, const Vote& v);
    std::optional<Vote> get(const std::string& orgId, const std::string& proposalId, const std::string& agentId);
    std::vector<Vote> listByProposal(const std::string& orgId, const std::string& proposalId);

private:
    RocksDBWrapper& db_;
    IClock& clock_;
};

}  // namespace voterpool
