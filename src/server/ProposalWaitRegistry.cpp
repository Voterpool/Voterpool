#include "server/ProposalWaitRegistry.h"

#include "core/Metrics.h"

#include <algorithm>

namespace voterpool {

void ProposalWaitRegistry::adjustGauge(std::int64_t delta) {
    activeCount_ = static_cast<std::size_t>(
        std::max<std::int64_t>(0, static_cast<std::int64_t>(activeCount_) + delta));
    MetricsRegistry::instance().setGauge("voterpool_wait_proposal_close_active", {},
                                         static_cast<std::int64_t>(activeCount_));
}

bool ProposalWaitRegistry::waitForClose(const std::string& proposalId,
                                        std::chrono::steady_clock::time_point deadline) {
    auto w = std::make_shared<Waiter>();
    {
        std::lock_guard lock(mutex_);
        waiters_[proposalId].push_back(w);
        adjustGauge(+1);
    }
    {
        std::unique_lock lk(w->m);
        w->cv.wait_until(lk, deadline, [&] { return w->fired; });
    }
    const bool fired = w->fired;
    {
        std::lock_guard lock(mutex_);
        auto it = waiters_.find(proposalId);
        if (it != waiters_.end()) {
            auto& list = it->second;
            list.erase(std::remove(list.begin(), list.end(), w), list.end());
            if (list.empty()) waiters_.erase(it);
        }
        adjustGauge(-1);
    }
    return fired;
}

void ProposalWaitRegistry::notifyClosed(const std::string& proposalId) {
    std::vector<std::shared_ptr<Waiter>> toFire;
    {
        std::lock_guard lock(mutex_);
        auto it = waiters_.find(proposalId);
        if (it == waiters_.end()) return;
        toFire = std::move(it->second);
        waiters_.erase(it);
    }
    for (auto& w : toFire) {
        {
            std::lock_guard lk(w->m);
            w->fired = true;
        }
        w->cv.notify_all();
    }
    // Подписчики вычтут себя в waitForClose после пробуждения.
}

std::size_t ProposalWaitRegistry::activeWaiters() const {
    std::lock_guard lock(mutex_);
    return activeCount_;
}

}  // namespace voterpool
