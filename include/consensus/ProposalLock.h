#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace voterpool {

class ProposalLockRegistry {
public:
    class Guard {
    public:
        Guard() = default;
        explicit Guard(std::mutex* m) : m_(m) { if (m_) m_->lock(); }
        ~Guard() { if (m_) m_->unlock(); }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        Guard(Guard&& o) noexcept : m_(o.m_) { o.m_ = nullptr; }
        Guard& operator=(Guard&& o) noexcept {
            if (m_) m_->unlock();
            m_ = o.m_;
            o.m_ = nullptr;
            return *this;
        }

    private:
        std::mutex* m_ = nullptr;
    };

    Guard acquire(const std::string& proposalId) {
        std::shared_ptr<std::mutex> entry;
        {
            std::lock_guard lock(mapMutex_);
            auto& slot = locks_[proposalId];
            if (!slot) slot = std::make_shared<std::mutex>();
            entry = slot;
        }
        return Guard(entry.get());
    }

    void forget(const std::string& proposalId) {
        std::lock_guard lock(mapMutex_);
        locks_.erase(proposalId);
    }

private:
    std::mutex mapMutex_;
    std::map<std::string, std::shared_ptr<std::mutex>> locks_;
};

}  // namespace voterpool
