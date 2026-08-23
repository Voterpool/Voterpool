#pragma once

#include "core/IClock.h"
#include "domain/Membership.h"
#include "domain/Organization.h"
#include "storage/Codec.h"
#include "storage/RocksDBWrapper.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace voterpool {

class OrgRepository {
public:
    explicit OrgRepository(RocksDBWrapper& db, IClock& clock) : db_(db), clock_(clock) {}

    bool put(const Organization& org);
    void put(rocksdb::WriteBatch& batch, const Organization& org);
    std::optional<Organization> get(const std::string& orgId);
    std::int64_t countActive();
    std::int64_t countDissolved();

    std::optional<Membership> getMembership(const std::string& orgId, const std::string& agentId);
    bool putMembership(const Membership& m);
    void putMembership(rocksdb::WriteBatch& batch, const Membership& m);
    bool deleteMembership(const Membership& m);
    void deleteMembership(rocksdb::WriteBatch& batch, const std::string& orgId, const std::string& agentId);
    std::vector<Membership> listMembers(const std::string& orgId);
    std::vector<Membership> listOrgsOfAgent(const std::string& agentId);
    int countActiveMembers(const std::string& orgId);

private:
    RocksDBWrapper& db_;
    IClock& clock_;
};

}  // namespace voterpool
