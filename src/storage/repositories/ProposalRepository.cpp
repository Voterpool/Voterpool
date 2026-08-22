#include "storage/repositories/ProposalRepository.h"

#include "storage/Keys.h"

namespace voterpool {

void ProposalRepository::put(rocksdb::WriteBatch& batch, const Proposal& p) {
    db_.put(batch, "cf_proposals", Keys::proposal(p.org_id, p.proposal_id), Codec::serializeProposal(p));
}

bool ProposalRepository::put(const Proposal& p) {
    rocksdb::WriteBatch batch;
    put(batch, p);
    return db_.commit(batch);
}

std::optional<Proposal> ProposalRepository::get(const std::string& orgId, const std::string& proposalId) {
    auto v = db_.get("cf_proposals", Keys::proposal(orgId, proposalId));
    if (!v) return std::nullopt;
    return Codec::deserializeProposal(*v);
}

std::vector<Proposal> ProposalRepository::listByOrgAll() {
    std::vector<Proposal> out;
    auto it = db_.newIterator("cf_proposals");
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        auto p = Codec::deserializeProposal(it->value().ToString());
        if (p) out.push_back(std::move(*p));
    }
    return out;
}

void ProposalRepository::remove(rocksdb::WriteBatch& batch, const std::string& orgId, const std::string& proposalId) {
    db_.remove(batch, "cf_proposals", Keys::proposal(orgId, proposalId));
}

std::vector<Proposal> ProposalRepository::listByOrg(const std::string& orgId) {
    std::vector<Proposal> out;
    auto it = db_.newIterator("cf_proposals");
    for (it->Seek(Keys::proposalPrefix(orgId)); it->Valid(); it->Next()) {
        std::string k = it->key().ToString();
        if (k.rfind(Keys::proposalPrefix(orgId), 0) != 0) break;
        auto p = Codec::deserializeProposal(it->value().ToString());
        if (p) out.push_back(std::move(*p));
    }
    return out;
}

void ProposalRepository::addActiveIndex(rocksdb::WriteBatch& batch, const Proposal& p) {
    db_.put(batch, "cf_indexes", Keys::activeProposal(p.expires_at, p.proposal_id), p.org_id);
}

void ProposalRepository::removeActiveIndex(rocksdb::WriteBatch& batch, const Proposal& p) {
    db_.remove(batch, "cf_indexes", Keys::activeProposal(p.expires_at, p.proposal_id));
}

void ProposalRepository::addLookup(rocksdb::WriteBatch& batch, const Proposal& p) {
    db_.put(batch, "cf_indexes", Keys::proposalLookup(p.proposal_id), p.org_id);
}

std::string ProposalRepository::lookupOrg(const std::string& proposalId) {
    auto v = db_.get("cf_indexes", Keys::proposalLookup(proposalId));
    return v.value_or("");
}

std::int64_t ProposalRepository::countActiveGauge() {
    std::int64_t n = 0;
    auto it = db_.newIterator("cf_proposals");
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        auto p = Codec::deserializeProposal(it->value().ToString());
        if (p && p->status == ProposalStatus::ACTIVE) ++n;
    }
    return n;
}

}  // namespace voterpool
