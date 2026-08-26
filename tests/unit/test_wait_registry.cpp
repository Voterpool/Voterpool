// ProposalWaitRegistry: пробуждение подписчиков закрытия предложения
// без lost-wakeup и с конкурентными ожиданиями (task 3.3).
#include "server/ProposalWaitRegistry.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace voterpool;
using namespace std::chrono_literals;

TEST(ProposalWaitRegistry, NotifyWakesWaiterBeforeDeadline) {
    ProposalWaitRegistry r;
    auto deadline = std::chrono::steady_clock::now() + 2s;

    std::atomic<bool> woke{false};
    std::thread waiter([&] {
        bool fired = r.waitForClose("prop-1", deadline);
        woke = fired;
    });
    std::this_thread::sleep_for(50ms);  // подписчик успел зарегистрироваться
    EXPECT_EQ(r.activeWaiters(), 1u);
    r.notifyClosed("prop-1");
    waiter.join();
    EXPECT_TRUE(woke);
    EXPECT_EQ(r.activeWaiters(), 0u);
}

// Дизайн D6: notify не «теряется», потому что терминальность подписчик
// перепроверяет сам; здесь проверяем, что поздняя регистрация после
// notify НЕ блокируется до дедлайна, если статус перепроверен (модель
// registry = чистый ускоритель). Прямой контракт: waitForClose до дедлайна
// при отсутствии notify возвращает false.
TEST(ProposalWaitRegistry, TimeoutWithoutNotifyReturnsFalse) {
    ProposalWaitRegistry r;
    auto deadline = std::chrono::steady_clock::now() + 120ms;
    EXPECT_FALSE(r.waitForClose("prop-2", deadline));
    EXPECT_EQ(r.activeWaiters(), 0u);
}

// Один голос закрывает N параллельных ожидающих расширенного сценария
// test_concurrency: все просыпаются, счётчик активных обнуляется.
TEST(ProposalWaitRegistry, NotifyWakesAllConcurrentWaiters) {
    ProposalWaitRegistry r;
    constexpr int kN = 8;
    std::atomic<int> woken{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < kN; ++i) {
        threads.emplace_back([&] {
            if (r.waitForClose("prop-multi",
                               std::chrono::steady_clock::now() + 3s))
                ++woken;
        });
    }
    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(r.activeWaiters(), static_cast<std::size_t>(kN));
    r.notifyClosed("prop-multi");
    for (auto& t : threads) t.join();
    EXPECT_EQ(woken.load(), kN);
    EXPECT_EQ(r.activeWaiters(), 0u);
}
