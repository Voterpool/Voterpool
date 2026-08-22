#include "server/Workers.h"

#include "core/Metrics.h"
#include "server/SseHub.h"

#include <concurrentqueue.h>

#include <thread>

namespace voterpool {

class Workers::QueueImpl {
public:
    moodycamel::ConcurrentQueue<SseEvent> q;
};

Workers::Workers(const AppConfig& cfg, IClock& clock, SseHub& hub, std::function<void(std::int64_t)> ttlTick)
    : cfg_(cfg), clock_(clock), hub_(hub), ttlTick_(std::move(ttlTick)), queue_(std::make_unique<QueueImpl>()) {}

Workers::~Workers() { stop(); }

void Workers::start() {
    stopping_ = false;
    ttlThread_ = std::jthread([this](std::stop_token st) { ttlLoop(std::move(st)); });
    dispatcherThread_ = std::jthread([this](std::stop_token st) { dispatcherLoop(std::move(st)); });
    heartbeatThread_ = std::jthread([this](std::stop_token st) {
        int elapsedMs = 0;
        while (!st.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (st.stop_requested()) break;
            elapsedMs += 100;
            if (elapsedMs >= cfg_.sse.heartbeat_interval_sec * 1000) {
                elapsedMs = 0;
                hub_.heartbeat();
            }
        }
    });
}

void Workers::stop() {
    bool expected = false;
    if (!stopping_.compare_exchange_strong(expected, true)) return;
    cv_.notify_all();
    ttlThread_.request_stop();
    dispatcherThread_.request_stop();
    heartbeatThread_.request_stop();
    cv_.notify_all();
    if (ttlThread_.joinable()) ttlThread_.join();
    if (dispatcherThread_.joinable()) {
        dispatcherThread_.join();
    }
    if (heartbeatThread_.joinable()) heartbeatThread_.join();
}

void Workers::enqueue(SseEvent event) {
    queue_->q.enqueue(std::move(event));
    queueDepth_.fetch_add(1, std::memory_order_relaxed);
    cv_.notify_one();
}

void Workers::ttlLoop(std::stop_token st) {
    while (!st.stop_requested()) {
        auto t0 = std::chrono::steady_clock::now();
        std::int64_t now = clock_.nowSec();
        ttlTick_(now);
        MetricsRegistry::instance().incCounter("voterpool_ttl_scans_total");
        auto dt = std::chrono::steady_clock::now() - t0;
        MetricsRegistry::instance().observe(
            "voterpool_ttl_scan_duration_seconds", std::chrono::duration<double>(dt).count());
        for (int i = 0; i < 10 && !st.stop_requested(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void Workers::dispatcherLoop(std::stop_token st) {
    std::vector<SseEvent> batch;
    while (!st.stop_requested()) {
        size_t n = 0;
        {
            SseEvent ev;
            while (n < 1000 && queue_->q.try_dequeue(ev)) {
                batch.push_back(ev);
                ++n;
            }
        }
        if (batch.empty()) {
            std::unique_lock lock(cvMutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(50));
            continue;
        }
        for (auto& ev : batch) hub_.deliver(ev);
        queueDepth_.fetch_sub(batch.size(), std::memory_order_relaxed);
        MetricsRegistry::instance().setGauge("voterpool_sse_queue_depth", {},
                                             static_cast<std::int64_t>(queueDepth_.load()));
        batch.clear();
    }
    SseEvent ev;
    while (queue_->q.try_dequeue(ev)) {
        hub_.deliver(ev);
        queueDepth_.fetch_sub(1, std::memory_order_relaxed);
    }
}

}  // namespace voterpool
