#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace voterpool {

class IClock {
public:
    virtual ~IClock() = default;
    virtual std::int64_t nowSec() const = 0;
    virtual std::int64_t nowMilli() const = 0;
};

class SystemClock : public IClock {
public:
    static IClock& instance() {
        static SystemClock c;
        return c;
    }
    std::int64_t nowSec() const override {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }
    std::int64_t nowMilli() const override {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }
};

class MockClock : public IClock {
public:
    explicit MockClock(std::int64_t startSec = 1700000000) : current_{startSec * 1000} {}
    std::int64_t nowSec() const override { return current_.load(std::memory_order_relaxed) / 1000; }
    std::int64_t nowMilli() const override { return current_.load(std::memory_order_relaxed); }
    void advanceSeconds(std::int64_t s) { current_.fetch_add(s * 1000, std::memory_order_relaxed); }
    void advanceMillis(std::int64_t ms) { current_.fetch_add(ms, std::memory_order_relaxed); }

private:
    std::atomic<std::int64_t> current_;
};

}  // namespace voterpool
