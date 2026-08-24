#include "consensus/ProposalLock.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using voterpool::KeyedMutexRegistry;
using voterpool::ProposalLockRegistry;

TEST(ProposalLockRegistry, ForgetWhileGuardAliveKeepsMutexAlive) {
    ProposalLockRegistry registry;
    auto guard = registry.acquire("p1");
    registry.forget("p1");

    std::atomic<bool> reacquired{false};
    std::thread t([&] {
        auto g2 = registry.acquire("p1");
        reacquired.store(true);
    });
    t.join();
    EXPECT_TRUE(reacquired.load()) << "повторный acquire после forget не блокируется (новый мьютекс)";
}

TEST(ProposalLockRegistry, SameIdSerializesAcrossThreads) {
    ProposalLockRegistry registry;
    auto guard = registry.acquire("p2");

    std::atomic<bool> entered{false};
    std::thread t([&] {
        auto g = registry.acquire("p2");
        entered.store(true);
    });

    for (int i = 0; i < 100 && !entered.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_FALSE(entered.load()) << "второй поток обязан ждать внутри критической секции";

    guard = ProposalLockRegistry::Guard();
    t.join();
    EXPECT_TRUE(entered.load());
}

TEST(ProposalLockRegistry, DifferentIdsDoNotBlockEachOther) {
    ProposalLockRegistry registry;
    auto g1 = registry.acquire("a");
    std::atomic<bool> entered{false};
    std::thread t([&] {
        auto g2 = registry.acquire("b");
        entered.store(true);
    });
    t.join();
    EXPECT_TRUE(entered.load());
}

TEST(ProposalLockRegistry, MoveTransfersOwnershipWithoutDoubleUnlock) {
    ProposalLockRegistry registry;
    auto source = registry.acquire("p3");
    ProposalLockRegistry::Guard moved(std::move(source));

    std::thread t([&] {
        auto blocker = registry.acquire("p3");
    });
    for (int i = 0; i < 50; ++i) {
        if (t.joinable() && i > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    moved = ProposalLockRegistry::Guard();
    t.join();
}

TEST(ProposalLockRegistry, ConcurrentAcquireAndForgetIsSafe) {
    ProposalLockRegistry registry;
    constexpr int kIterations = 200;
    std::vector<std::thread> threads;
    threads.emplace_back([&] {
        for (int i = 0; i < kIterations; ++i) {
            auto g = registry.acquire("hot");
            registry.forget("hot");
        }
    });
    threads.emplace_back([&] {
        for (int i = 0; i < kIterations; ++i) {
            auto g = registry.acquire("hot");
        }
    });
    for (auto& t : threads) t.join();
}

TEST(KeyedMutexRegistry, TwoInstancesWithSameKeysAreIndependent) {
    KeyedMutexRegistry proposals;
    KeyedMutexRegistry orgs;

    auto proposalGuard = proposals.acquire("shared-id");
    std::atomic<bool> entered{false};
    std::thread t([&] {
        auto orgGuard = orgs.acquire("shared-id");
        entered.store(true);
    });
    t.join();
    EXPECT_TRUE(entered.load())
        << "одинаковый ключ в разных реестрах (proposal/org) не блокирует друг друга";
}

TEST(KeyedMutexRegistry, SameKeyStillSerializesWithinOneInstance) {
    KeyedMutexRegistry orgs;
    auto guard = orgs.acquire("org-1");
    std::atomic<bool> entered{false};
    std::thread t([&] {
        auto g2 = orgs.acquire("org-1");
        entered.store(true);
    });
    for (int i = 0; i < 100 && !entered.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_FALSE(entered.load()) << "внутри одного реестра тот же ключ сериализуется";
    guard = KeyedMutexRegistry::Guard();
    t.join();
    EXPECT_TRUE(entered.load());
}
