#pragma once

#include "domain/Organization.h"
#include "storage/RocksDBWrapper.h"

#include <set>
#include <string>
#include <vector>

namespace voterpool {

struct FeedCursor {
    std::int64_t revTs = -1;
    std::string orgId;
};

class IndexRepository {
public:
    explicit IndexRepository(RocksDBWrapper& db) : db_(db) {}

    void addPending(rocksdb::WriteBatch& batch, const std::string& orgId, const std::string& agentId, const std::string& ts);
    void removePending(rocksdb::WriteBatch& batch, const std::string& orgId, const std::string& agentId);
    bool hasPending(const std::string& orgId, const std::string& agentId);
    std::vector<std::string> listPending(const std::string& orgId);

    void addFeedActive(rocksdb::WriteBatch& batch, const Organization& org);
    void moveFeedToDissolved(rocksdb::WriteBatch& batch, const Organization& org);
    std::vector<std::string> scanFeedActive();

    void setTags(rocksdb::WriteBatch& batch, const Organization& org);
    void removeTags(rocksdb::WriteBatch& batch, const Organization& org);
    std::set<std::string> scanTag(const std::string& tag);

    void setCategory(rocksdb::WriteBatch& batch, const Organization& org);
    void removeCategory(rocksdb::WriteBatch& batch, const Organization& org);
    std::set<std::string> scanCategory(const std::string& categoryLowered);

    std::int64_t incrementJoinLimit(rocksdb::WriteBatch& batch, const std::string& orgId, std::int64_t daySec);
    std::int64_t getJoinLimit(const std::string& orgId, std::int64_t daySec);

private:
    static std::string orgFromKey(const std::string& key);

    RocksDBWrapper& db_;
};

}  // namespace voterpool
