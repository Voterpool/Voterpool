#pragma once

#include "core/Config.h"
#include "core/IClock.h"
#include "domain/Vote.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace voterpool {

class SseHub;
class ConsensusEngine;

class Workers {
public:
    Workers(const AppConfig& cfg, IClock& clock, SseHub& hub, std::function<void(std::int64_t)> ttlTick);
    ~Workers();

    void start();
    void stop();

    void enqueue(SseEvent event);
    size_t queueDepthApprox() const { return queueDepth_.load(std::memory_order_relaxed); }

private:
    void ttlLoop(std::stop_token st);
    void dispatcherLoop(std::stop_token st);

    AppConfig cfg_;
    IClock& clock_;
    SseHub& hub_;
    std::function<void(std::int64_t)> ttlTick_;

    class QueueImpl;
    std::unique_ptr<QueueImpl> queue_;
    std::atomic<size_t> queueDepth_{0};

    std::jthread ttlThread_;
    std::jthread dispatcherThread_;
    std::jthread heartbeatThread_;

    std::mutex cvMutex_;
    std::condition_variable cv_;
    std::atomic<bool> stopping_{false};
};

}  // namespace voterpool
