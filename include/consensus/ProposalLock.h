#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace voterpool {

// Контракт реестра (design D2):
//  1) forget(key) вызывается только после успешного коммита
//     терминального статуса предложения (включая путь роспуска:
//     EXPIRED-записи фиксируются тем же коммитом, что и статус
//     DISSOLVED организации).
//  2) Внутри критической секции из acquire() состояние предложения
//     и статус его организации перепроверяются из хранилища.
//
// После forget() повторный acquire() того же ключа создаёт новый
// экземпляр мьютекса, поэтому прежний держатель Guard и новый acquirer
// могут временно находиться в критических секциях разных объектов.
// Это безопасно благодаря (1)+(2): опоздавший избиратель прочтёт
// терминальный статус и получит -32003 Conflict, голос в распущенную
// организацию — -32004.
//
// Иерархия локов исключает тупики: операция держит не более одного
// proposal-лока и не более одного org-лока; если нужны оба — сначала
// proposal, затем organization; держатель org-лока никогда не ожидает
// proposal-локов. Отдельные экземпляры реестра (proposal/org) независимы.
class KeyedMutexRegistry {
public:
    class Guard {
    public:
        Guard() = default;
        explicit Guard(std::shared_ptr<std::mutex> m) : m_(std::move(m)) {
            if (m_) m_->lock();
        }
        ~Guard() {
            if (m_) m_->unlock();
        }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        Guard(Guard&& o) noexcept : m_(std::move(o.m_)) { o.m_.reset(); }
        Guard& operator=(Guard&& o) noexcept {
            if (this != &o) {
                if (m_) m_->unlock();
                m_ = std::move(o.m_);
                o.m_.reset();
            }
            return *this;
        }

    private:
        std::shared_ptr<std::mutex> m_;
    };

    Guard acquire(const std::string& key) {
        std::shared_ptr<std::mutex> entry;
        {
            std::lock_guard lock(mapMutex_);
            auto& slot = locks_[key];
            if (!slot) slot = std::make_shared<std::mutex>();
            entry = slot;
        }
        return Guard(entry);
    }

    // Удаляет запись реестра. Живые Guard'ы удерживают мьютекс через
    // shared_ptr; физическое удаление откладывается до последнего unlock().
    void forget(const std::string& key) {
        std::lock_guard lock(mapMutex_);
        locks_.erase(key);
    }

private:
    std::mutex mapMutex_;
    std::map<std::string, std::shared_ptr<std::mutex>> locks_;
};

using ProposalLockRegistry = KeyedMutexRegistry;

}  // namespace voterpool
