#include "storage/repositories/IndexRepository.h"

#include "storage/Keys.h"

#include <algorithm>

namespace voterpool {

void IndexRepository::addPending(rocksdb::WriteBatch& batch, const std::string& orgId, const std::string& agentId, const std::string& ts) {
    db_.put(batch, "cf_indexes", Keys::pending(orgId, agentId), ts);
}

void IndexRepository::removePending(rocksdb::WriteBatch& batch, const std::string& orgId, const std::string& agentId) {
    db_.remove(batch, "cf_indexes", Keys::pending(orgId, agentId));
}

bool IndexRepository::hasPending(const std::string& orgId, const std::string& agentId) {
    return db_.get("cf_indexes", Keys::pending(orgId, agentId)).has_value();
}

std::vector<std::string> IndexRepository::listPending(const std::string& orgId) {
    std::vector<std::string> out;
    auto it = db_.newIterator("cf_indexes");
    for (it->Seek(Keys::pendingPrefix(orgId)); it->Valid(); it->Next()) {
        std::string k = it->key().ToString();
        if (k.rfind(Keys::pendingPrefix(orgId), 0) != 0) break;
        out.push_back(k.substr(Keys::pendingPrefix(orgId).size()));
    }
    return out;
}

void IndexRepository::addFeedActive(rocksdb::WriteBatch& batch, const Organization& org) {
    db_.put(batch, "cf_indexes", Keys::orgFeed("ACTIVE", org.created_at, org.org_id), "");
}

void IndexRepository::moveFeedToDissolved(rocksdb::WriteBatch& batch, const Organization& org) {
    db_.remove(batch, "cf_indexes", Keys::orgFeed("ACTIVE", org.created_at, org.org_id));
    db_.put(batch, "cf_indexes", Keys::orgFeed("DISSOLVED", org.created_at, org.org_id), "");
}

std::vector<std::string> IndexRepository::scanFeedActive() {
    // Мульти-бакетный скан (docs/16 §3.2): собираем записи всех K префиксов
    // и восстанавливаем ГЛОБАЛЬНЫЙ порядок ленты по rev-таймстампу
    // (rev asc == created_at desc — как в legacy-раскладке).
    struct FeedEntry {
        std::int64_t rev;
        std::string orgId;
    };
    std::vector<FeedEntry> entries;
    auto it = db_.newIterator("cf_indexes");
    for (int b = 0; b < Keys::kBucketCount; ++b) {
        const std::string prefix = Keys::orgFeedActivePrefixIn(b);
        for (it->Seek(prefix); it->Valid(); it->Next()) {
            std::string k = it->key().ToString();
            if (k.rfind(prefix, 0) != 0) break;
            const std::size_t revStart = prefix.size();
            std::int64_t rev = 0;
            try {
                rev = std::stoll(k.substr(revStart, 19));
            } catch (...) {
                continue;
            }
            entries.push_back({rev, k.substr(revStart + 20)});
        }
    }
    std::sort(entries.begin(), entries.end(),
              [](const FeedEntry& a, const FeedEntry& c) { return a.rev < c.rev; });
    std::vector<std::string> out;
    out.reserve(entries.size());
    for (auto& e : entries) out.push_back(std::move(e.orgId));
    return out;
}

std::string IndexRepository::orgFromKey(const std::string& key) {
    const size_t sep = key.rfind(':');
    return sep == std::string::npos ? "" : key.substr(sep + 1);
}

void IndexRepository::setTags(rocksdb::WriteBatch& batch, const Organization& org) {
    for (const auto& t : org.tags) {
        db_.put(batch, "cf_indexes", Keys::tag(Keys::tagLower(t), org.org_id), "");
    }
}

void IndexRepository::removeTags(rocksdb::WriteBatch& batch, const Organization& org) {
    for (const auto& t : org.tags) {
        db_.remove(batch, "cf_indexes", Keys::tag(Keys::tagLower(t), org.org_id));
    }
}

std::set<std::string> IndexRepository::scanTag(const std::string& tagLowered) {
    std::set<std::string> out;
    auto it = db_.newIterator("cf_indexes");
    for (int b = 0; b < Keys::kBucketCount; ++b) {
        const std::string prefix = Keys::tagPrefixIn(b, tagLowered);
        for (it->Seek(prefix); it->Valid(); it->Next()) {
            std::string k = it->key().ToString();
            if (k.rfind(prefix, 0) != 0) break;
            out.insert(orgFromKey(k));
        }
    }
    return out;
}

void IndexRepository::setCategory(rocksdb::WriteBatch& batch, const Organization& org) {
    if (org.category.empty()) return;
    db_.put(batch, "cf_indexes", Keys::category(Keys::nameLower(org.category), org.org_id), "");
}

void IndexRepository::removeCategory(rocksdb::WriteBatch& batch, const Organization& org) {
    if (org.category.empty()) return;
    db_.remove(batch, "cf_indexes", Keys::category(Keys::nameLower(org.category), org.org_id));
}

std::set<std::string> IndexRepository::scanCategory(const std::string& categoryLowered) {
    std::set<std::string> out;
    auto it = db_.newIterator("cf_indexes");
    for (int b = 0; b < Keys::kBucketCount; ++b) {
        const std::string prefix = Keys::categoryPrefixIn(b, categoryLowered);
        for (it->Seek(prefix); it->Valid(); it->Next()) {
            std::string k = it->key().ToString();
            if (k.rfind(prefix, 0) != 0) break;
            out.insert(orgFromKey(k));
        }
    }
    return out;
}

std::int64_t IndexRepository::incrementJoinLimit(rocksdb::WriteBatch& batch, const std::string& orgId, std::int64_t daySec) {
    std::string key = Keys::joinLimit(orgId, daySec);
    auto cur = db_.get("cf_indexes", key);
    std::int64_t next = cur ? std::stoll(*cur) + 1 : 1;
    db_.put(batch, "cf_indexes", key, std::to_string(next));
    return next;
}

std::int64_t IndexRepository::getJoinLimit(const std::string& orgId, std::int64_t daySec) {
    auto cur = db_.get("cf_indexes", Keys::joinLimit(orgId, daySec));
    return cur ? std::stoll(*cur) : 0;
}

}  // namespace voterpool
