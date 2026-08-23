#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace voterpool {

// Контракт реестра (design D2):
//  1) forget(proposalId) вызывается только после успешного коммита
//     терминального статуса предложения.
//  2) Внутри критической секции из acquire() состояние предложения
//     перепроверяется из хранилища.
//
// После forget() повторный acquire() того же id создаёт новый экземпляр
// мьютекса, поэтому прежний держатель Guard и новый acquirer могут временно
// находиться в критических секциях разных объектов. Это безопасно благодаря
// (1)+(2): опоздавший избиратель прочтёт терминальный статус и получит
// -32003 Conflict.
class ProposalLockRegistry {
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

    Guard acquire(const std::string& proposalId) {
        std::shared_ptr<std::mutex> entry;
        {
            std::lock_guard lock(mapMutex_);
            auto& slot = locks_[proposalId];
            if (!slot) slot = std::make_shared<std::mutex>();
            entry = slot;
        }
        return Guard(entry);
    }

    // Удаляет запись реестра. Живые Guard'ы удерживают мьютекс через
    // shared_ptr; физическое удаление откладывается до последнего unlock().
    void forget(const std::string& proposalId) {
        std::lock_guard lock(mapMutex_);
        locks_.erase(proposalId);
    }

private:
    std::mutex mapMutex_;
    std::map<std::string, std::shared_ptr<std::mutex>> locks_;
};

}  // namespace voterpool
