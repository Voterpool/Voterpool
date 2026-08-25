#include "storage/repositories/OrgRepository.h"

#include "storage/Keys.h"

namespace voterpool {

bool OrgRepository::put(const Organization& org) {
    rocksdb::WriteBatch batch;
    put(batch, org);
    return db_.commit(batch);
}

void OrgRepository::put(rocksdb::WriteBatch& batch, const Organization& org) {
    db_.put(batch, "cf_organizations", Keys::org(org.org_id), Codec::serializeOrg(org));
}

std::optional<Organization> OrgRepository::get(const std::string& orgId) {
    auto v = db_.get("cf_organizations", Keys::org(orgId));
    if (!v) return std::nullopt;
    return Codec::deserializeOrg(*v);
}

std::int64_t OrgRepository::countActive() {
    std::int64_t n = 0;
    auto it = db_.newIterator("cf_organizations");
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        auto org = Codec::deserializeOrg(it->value().ToString());
        if (org && org->status == OrgStatus::ACTIVE) ++n;
    }
    return n;
}

std::int64_t OrgRepository::countDissolved() {
    std::int64_t n = 0;
    auto it = db_.newIterator("cf_organizations");
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        auto org = Codec::deserializeOrg(it->value().ToString());
        if (org && org->status == OrgStatus::DISSOLVED) ++n;
    }
    return n;
}

std::optional<Membership> OrgRepository::getMembership(const std::string& orgId, const std::string& agentId) {
    auto v = db_.get("cf_memberships", Keys::membership(orgId, agentId));
    if (!v) return std::nullopt;
    return Codec::deserializeMembership(*v);
}

void OrgRepository::putMembership(rocksdb::WriteBatch& batch, const Membership& m) {
    db_.put(batch, "cf_memberships", Keys::membership(m.org_id, m.agent_id), Codec::serializeMembership(m));
    db_.put(batch, "cf_agent_orgs", Keys::agentOrgs(m.agent_id, m.org_id),
            std::string(toString(m.role)) + "/" + toString(m.status));
}

bool OrgRepository::putMembership(const Membership& m) {
    rocksdb::WriteBatch batch;
    putMembership(batch, m);
    return db_.commit(batch);
}

void OrgRepository::deleteMembership(rocksdb::WriteBatch& batch, const std::string& orgId, const std::string& agentId) {
    db_.remove(batch, "cf_memberships", Keys::membership(orgId, agentId));
    db_.remove(batch, "cf_agent_orgs", Keys::agentOrgs(agentId, orgId));
}

bool OrgRepository::deleteMembership(const Membership& m) {
    rocksdb::WriteBatch batch;
    deleteMembership(batch, m.org_id, m.agent_id);
    return db_.commit(batch);
}

std::vector<Membership> OrgRepository::listMembers(const std::string& orgId) {
    std::vector<Membership> out;
    auto it = db_.newIterator("cf_memberships");
    for (it->Seek(Keys::membershipPrefix(orgId)); it->Valid(); it->Next()) {
        std::string k = it->key().ToString();
        if (k.rfind(Keys::membershipPrefix(orgId), 0) != 0) break;
        auto m = Codec::deserializeMembership(it->value().ToString());
        if (m) out.push_back(*m);
    }
    return out;
}

std::vector<Membership> OrgRepository::listOrgsOfAgent(const std::string& agentId) {
    std::vector<Membership> out;
    auto iter = db_.newIterator("cf_agent_orgs");
    for (iter->Seek(Keys::agentOrgsPrefix(agentId)); iter->Valid(); iter->Next()) {
        std::string k = iter->key().ToString();
        if (k.rfind(Keys::agentOrgsPrefix(agentId), 0) != 0) break;
        std::string rest = k.substr(Keys::agentOrgsPrefix(agentId).size());
        std::string orgId = rest.substr(0, rest.find(':'));
        auto m = getMembership(orgId, agentId);
        if (m) out.push_back(*m);
    }
    return out;
}

int OrgRepository::countActiveMembers(const std::string& orgId) {
    int n = 0;
    for (const auto& m : listMembers(orgId)) {
        if (m.status == MemberStatus::ACTIVE) ++n;
    }
    return n;
}

}  // namespace voterpool
