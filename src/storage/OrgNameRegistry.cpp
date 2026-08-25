#include "storage/OrgNameRegistry.h"

#include "domain/Enums.h"
#include "storage/Codec.h"
#include "storage/Keys.h"
#include "storage/RocksDBWrapper.h"

#include <algorithm>

namespace voterpool {
namespace {

std::string lowered(std::string s) { return Keys::nameLower(std::move(s)); }

}  // namespace

void OrgNameRegistry::load(RocksDBWrapper& db) {
    std::vector<Entry> fresh;
    auto it = db.newIterator("cf_organizations");
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        auto org = Codec::deserializeOrg(it->value().ToString());
        if (org && org->status == OrgStatus::ACTIVE) {
            fresh.push_back(Entry{org->org_id, lowered(org->name)});
        }
    }
    std::unique_lock lock(mutex_);
    entries_ = std::move(fresh);
    index_.clear();
    index_.reserve(entries_.size());
    for (size_t i = 0; i < entries_.size(); ++i) index_[entries_[i].org_id] = i;
}

std::set<std::string> OrgNameRegistry::matchQuery(const std::string& queryLowered) const {
    const std::string q = lowered(queryLowered);
    std::set<std::string> out;
    if (q.empty()) return out;
    std::shared_lock lock(mutex_);
    for (const Entry& e : entries_) {
        if (e.name_lower.find(q) != std::string::npos) out.insert(e.org_id);
    }
    return out;
}

std::optional<std::string> OrgNameRegistry::findActiveByName(const std::string& nameLowered) const {
    const std::string n = lowered(nameLowered);
    std::shared_lock lock(mutex_);
    for (const Entry& e : entries_) {
        if (e.name_lower == n) return e.org_id;
    }
    return std::nullopt;
}

void OrgNameRegistry::add(const std::string& orgId, const std::string& nameLowered) {
    std::unique_lock lock(mutex_);
    if (index_.count(orgId)) return;
    index_[orgId] = entries_.size();
    entries_.push_back(Entry{orgId, lowered(nameLowered)});
}

void OrgNameRegistry::rename(const std::string& orgId, const std::string& newNameLowered) {
    std::unique_lock lock(mutex_);
    auto it = index_.find(orgId);
    if (it == index_.end()) return;
    entries_[it->second].name_lower = lowered(newNameLowered);
}

void OrgNameRegistry::erase(const std::string& orgId) {
    std::unique_lock lock(mutex_);
    auto it = index_.find(orgId);
    if (it == index_.end()) return;
    const size_t pos = it->second;
    const size_t last = entries_.size() - 1;
    if (pos != last) {
        entries_[pos] = std::move(entries_[last]);
        index_[entries_[pos].org_id] = pos;
    }
    entries_.pop_back();
    index_.erase(it);
}

size_t OrgNameRegistry::size() const {
    std::shared_lock lock(mutex_);
    return entries_.size();
}

}  // namespace voterpool
