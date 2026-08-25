#pragma once

// Directory plane (docs/16 §3.1, change add-shard-ready-ports).
// Единственная точка доступа к каталогу организаций: поиск по именам,
// вторичные индексы тегов/категорий, лента ACTIVE и резолв
// proposal→organization. Реализации: scaling::LocalDirectory.

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace voterpool::scaling {

class IDirectory {
public:
    virtual ~IDirectory() = default;

    // Подстрочный поиск по нижнему регистру названия; DISSOLVED отсутствуют.
    virtual std::set<std::string> matchName(const std::string& nameLowered) = 0;

    // O(1) проверка дубликата имени при создании/переименовании.
    virtual std::optional<std::string> findActiveByName(const std::string& nameLowered) = 0;

    // Сопровождение реестра имён в такт жизненному циклу организации.
    virtual void indexOrg(const std::string& orgId, const std::string& nameLowered) = 0;
    virtual void renameOrg(const std::string& orgId, const std::string& newNameLowered) = 0;
    virtual void eraseOrg(const std::string& orgId) = 0;

    virtual std::set<std::string> scanTag(const std::string& tagLowered) = 0;
    virtual std::set<std::string> scanCategory(const std::string& categoryLowered) = 0;

    // Лента ACTIVE-организаций в порядке убывания created_at.
    virtual std::vector<std::string> scanFeedActive() = 0;

    // Резолв proposal_id → org_id (индекс proposal_lookup); неизвестный id → nullopt.
    virtual std::optional<std::string> resolveProposal(const std::string& proposalId) = 0;
};

}  // namespace voterpool::scaling
