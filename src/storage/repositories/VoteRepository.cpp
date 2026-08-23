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

std::vector<Vote> VoteRepository::listByProposal(const std::string& orgId, const std::string& proposalId) {
    std::vector<Vote> out;
    const std::string prefix = Keys::vote(orgId, proposalId, "");
    auto it = db_.newIterator("cf_votes");
    for (it->Seek(prefix); it->Valid(); it->Next()) {
        std::string k = it->key().ToString();
        if (k.rfind(prefix, 0) != 0) break;
        auto v = Codec::deserializeVote(it->value().ToString());
        if (v) out.push_back(std::move(*v));
    }
    return out;
}

}  // namespace voterpool
