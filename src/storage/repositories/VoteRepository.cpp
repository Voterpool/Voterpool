#include "storage/repositories/VoteRepository.h"

#include "storage/Keys.h"

namespace voterpool {

void VoteRepository::put(rocksdb::WriteBatch& batch, const std::string& orgId, const Vote& v) {
    db_.put(batch, "cf_votes", Keys::vote(orgId, v.proposal_id, v.agent_id), Codec::serializeVote(v));
}

std::optional<Vote> VoteRepository::get(const std::string& orgId, const std::string& proposalId, const std::string& agentId) {
    auto v = db_.get("cf_votes", Keys::vote(orgId, proposalId, agentId));
    if (!v) return std::nullopt;
    return Codec::deserializeVote(*v);
}

}  // namespace voterpool
