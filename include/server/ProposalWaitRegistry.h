#pragma once

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace voterpool {

class ProposalWaitRegistry {
public:
    bool waitForClose(const std::string& proposalId,
                      std::chrono::steady_clock::time_point deadline);

    void notifyClosed(const std::string& proposalId);

    std::size_t activeWaiters() const;

private:
    struct Waiter {
        std::mutex m;
        std::condition_variable cv;
        bool fired = false;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<std::shared_ptr<Waiter>>> waiters_;
    std::size_t activeCount_ = 0;

    void adjustGauge(std::int64_t delta);
};

}  // namespace voterpool
