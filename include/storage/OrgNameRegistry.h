#pragma once

#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace voterpool {

class RocksDBWrapper;

class OrgNameRegistry {
public:
    // Полная перезагрузка из cf_organizations (только ACTIVE).
    void load(RocksDBWrapper& db);

    // Подстрочный поиск по нижнему регистру названия; DISSOLVED отсутствуют.
    std::set<std::string> matchQuery(const std::string& queryLowered) const;

    // O(1) проверка дубликата: org_id ACTIVE-организации с таким именем.
    std::optional<std::string> findActiveByName(const std::string& nameLowered) const;

    void add(const std::string& orgId, const std::string& nameLowered);
    void rename(const std::string& orgId, const std::string& newNameLowered);
    void erase(const std::string& orgId);

    size_t size() const;

private:
    struct Entry {
        std::string org_id;
        std::string name_lower;
    };

    mutable std::shared_mutex mutex_;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> index_;  // org_id -> позиция в entries_
};

}  // namespace voterpool
