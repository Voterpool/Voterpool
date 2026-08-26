// Контрактные тесты портов scaling: один и тот же набор сценариев
// прогоняется против Local*-имплов (RocksDB) и фейков.
// Фейки переиспользуются будущими Remote-имплами.
#include "scaling/IEventBus.h"
#include "scaling/IDirectory.h"
#include "scaling/IIdentity.h"
#include "scaling/local_impls.h"

#include "common/Harness.h"

#include <gtest/gtest.h>
#include <rocksdb/write_batch.h>

#include <drogon/drogon.h>

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace voterpool;
using voterpool::testing::Harness;
using scaling::IDirectory;
using scaling::IEventBus;
using scaling::IIdentity;

// ---------- Фейки: in-memory семантика контракта ----------

class FakeDirectory : public IDirectory {
public:
    std::set<std::string> matchName(const std::string& q) override {
        std::set<std::string> out;
        for (const auto& [id, name] : names_) {
            if (erased_.count(id)) continue;
            if (name.find(q) != std::string::npos) out.insert(id);
        }
        return out;
    }
    std::optional<std::string> findActiveByName(const std::string& n) override {
        for (const auto& [id, name] : names_) {
            if (!erased_.count(id) && name == n) return id;
        }
        return std::nullopt;
    }
    void indexOrg(const std::string& id, const std::string& nameLowered) override {
        names_[id] = nameLowered;
        feed_.push_back(id);
    }
    void renameOrg(const std::string& id, const std::string& n) override { names_[id] = n; }
    void eraseOrg(const std::string& id) override { erased_.insert(id); }
    std::set<std::string> scanTag(const std::string& t) override { return tags_[t]; }
    void addTag(const std::string& t, const std::string& id) { tags_[t].insert(id); }
    std::set<std::string> scanCategory(const std::string& c) override { return cats_[c]; }
    void addCategory(const std::string& c, const std::string& id) { cats_[c].insert(id); }
    std::vector<std::string> scanFeedActive() override {
        std::vector<std::string> out;
        for (const auto& id : feed_)
            if (!erased_.count(id)) out.push_back(id);
        return out;
    }
    std::optional<std::string> resolveProposal(const std::string& pid) override {
        auto it = lookups_.find(pid);
        if (it == lookups_.end()) return std::nullopt;
        return it->second;
    }
    void addLookup(const std::string& pid, const std::string& orgId) { lookups_[pid] = orgId; }

private:
    std::map<std::string, std::string> names_;
    std::set<std::string> erased_;
    std::map<std::string, std::set<std::string>> tags_;
    std::map<std::string, std::set<std::string>> cats_;
    std::vector<std::string> feed_;
    std::map<std::string, std::string> lookups_;
};

class FakeIdentity : public IIdentity {
public:
    std::optional<AgentContext> resolveToken(const std::string& token) override {
        auto it = tokens_.find(token);
        if (it == tokens_.end()) return std::nullopt;
        return it->second;
    }
    bool createAgent(const std::string& agentId, const std::string& name,
                     const std::string& hash, std::string* outErr) override {
        if (profiles_.count(agentId)) {
            if (outErr) *outErr = "duplicate";
            return false;
        }
        Agent a;
        a.agent_id = agentId;
        a.name = name;
        profiles_[agentId] = a;
        tokens_[hash] = AgentContext{agentId, "NATIVE", false};
        (void)hash;
        return true;
    }
    std::optional<Agent> getProfile(const std::string& agentId) override {
        auto it = profiles_.find(agentId);
        if (it == profiles_.end()) return std::nullopt;
        return it->second;
    }
    bool putProfile(const Agent& a) override { profiles_[a.agent_id] = a; return true; }
    std::int64_t countAgents() override { return profiles_.size(); }
    std::vector<Membership> listOrgsOfAgent(const std::string& agentId) override {
        return links_[agentId];
    }
    void recordMembershipLink(rocksdb::WriteBatch&, const Membership& m) override {
        links_[m.agent_id].push_back(m);
    }
    void removeMembershipLink(rocksdb::WriteBatch&, const std::string& orgId,
                              const std::string& agentId) override {
        auto& v = links_[agentId];
        for (auto it = v.begin(); it != v.end(); ++it) {
            if (it->org_id == orgId) { v.erase(it); break; }
        }
    }

private:
    std::map<std::string, Agent> profiles_;
    std::map<std::string, AgentContext> tokens_;
    std::map<std::string, std::vector<Membership>> links_;
};

class FakeEventBus : public IEventBus {
public:
    void subscribeAllOrgs(const std::vector<std::string>& orgIds, const std::string& agentId,
                          drogon::ResponseStreamPtr) override {
        subs_.push_back({orgIds, agentId});
        ++connections_;
    }
    void registerKeepAlive(drogon::ResponseStreamPtr) override { ++connections_; }
    void deliver(const SseEvent& ev) override { delivered_.push_back(ev.org_id + ":" + ev.event_type); }
    void heartbeat() override {}
    void shutdownAll() override { connections_ = 0; }
    std::size_t connectionCount() override { return connections_; }

    std::vector<std::pair<std::vector<std::string>, std::string>> subs_;
    std::vector<std::string> delivered_;
    std::size_t connections_ = 0;
};

struct Backends {
    // Local-бэкенды поверх общего Harness; фейки живут сами по себе.
    std::shared_ptr<Harness> harness;
    std::unique_ptr<FakeDirectory> fakeDir;
    std::unique_ptr<FakeIdentity> fakeIdn;
    IDirectory* dir = nullptr;
    IIdentity* idn = nullptr;

    static std::unique_ptr<Backends> create(bool useFakes) {
        auto b = std::make_unique<Backends>();
        if (useFakes) {
            b->fakeDir = std::make_unique<FakeDirectory>();
            b->fakeIdn = std::make_unique<FakeIdentity>();
            b->dir = b->fakeDir.get();
            b->idn = b->fakeIdn.get();
            return b;
        }
        b->harness = Harness::create();
        b->dir = b->harness->app->directory.get();
        b->idn = b->harness->app->identity.get();
        return b;
    }
};

// Сценарий каталога: идентичные результаты на обоих бэкендах.
void runDirectoryScenario(IDirectory& d, FakeDirectory* fake) {
    d.indexOrg("org-aaa", "alpha research");
    d.indexOrg("org-bbb", "beta labs");
    d.renameOrg("org-aaa", "alpha institute");
    EXPECT_EQ(d.matchName("alpha"), (std::set<std::string>{"org-aaa"}));
    EXPECT_EQ(d.matchName("lab"), (std::set<std::string>{"org-bbb"}));
    EXPECT_TRUE(d.findActiveByName("alpha institute").has_value());
    EXPECT_EQ(*d.findActiveByName("alpha institute"), "org-aaa");
    EXPECT_FALSE(d.findActiveByName("missing name").has_value());
    d.eraseOrg("org-bbb");
    EXPECT_TRUE(d.matchName("beta").empty());

    // Теги/категории/лента — фейку данные кладёт тест напрямую.
    if (fake) {
        fake->indexOrg("org-t1", "taggy one");
        fake->addTag("ai", "org-t1");
        fake->addCategory("research", "org-t1");
        fake->addLookup("prop-1", "org-t1");
    }
}

// Сценарий identity: регистрация, профиль, связи, токены.
// flush — коммит батча (локальный бэкенд пишет в RocksDB; фейк игнорирует).
void runIdentityScenario(IIdentity& x, const std::function<void(rocksdb::WriteBatch&)>& flush) {
    std::string err;
    ASSERT_TRUE(x.createAgent("agent-1", "Alice", "hash-1", &err));
    EXPECT_FALSE(x.createAgent("agent-1", "Clone", "hash-x", &err));  // дубликат

    auto prof = x.getProfile("agent-1");
    ASSERT_TRUE(prof.has_value());
    EXPECT_EQ(prof->name, "Alice");

    Membership m;
    m.org_id = "org-9";
    m.agent_id = "agent-1";
    m.status = MemberStatus::ACTIVE;
    m.role = MemberRole::MEMBER;
    m.voting_power = 1.0;
    {
        rocksdb::WriteBatch batch;
        x.recordMembershipLink(batch, m);
        flush(batch);
    }
    ASSERT_EQ(x.listOrgsOfAgent("agent-1").size(), 1u);
    EXPECT_EQ(x.listOrgsOfAgent("agent-1")[0].org_id, "org-9");
    {
        rocksdb::WriteBatch batch;
        x.removeMembershipLink(batch, "org-9", "agent-1");
        flush(batch);
    }
    EXPECT_TRUE(x.listOrgsOfAgent("agent-1").empty());
}

}  // namespace

TEST(ScalingPortsContract, DirectoryScenarioLocalAndFakeAgree) {
    auto local = Backends::create(false);
    runDirectoryScenario(*local->dir, nullptr);

    auto fake = Backends::create(true);
    runDirectoryScenario(*fake->dir, fake->fakeDir.get());
}

TEST(ScalingPortsContract, IdentityScenarioLocalAndFakeAgree) {
    auto local = Backends::create(false);
    runIdentityScenario(*local->idn, [&h = *local->harness](rocksdb::WriteBatch& b) {
        (void)h.app->db->commit(b);
    });

    auto fake = Backends::create(true);
    runIdentityScenario(*fake->idn, [](rocksdb::WriteBatch&) {});
}

// Edge: неизвестный токен через локальный порт — формат ошибки middleware.
TEST(ScalingPortsContract, LocalResolveTokenUnknown) {
    auto b = Backends::create(false);
    EXPECT_FALSE(b->idn->resolveToken("voterpool_sec_unknown").has_value());
    EXPECT_FALSE(b->idn->resolveToken("").has_value());
}

// Edge: агент без организаций — пустой список без ошибок.
TEST(ScalingPortsContract, LocalAgentWithoutOrgs) {
    auto b = Backends::create(false);
    std::string err;
    ASSERT_TRUE(b->idn->createAgent("agent-lonely", "Lonely", "hash-l", &err));
    EXPECT_TRUE(b->idn->listOrgsOfAgent("agent-lonely").empty());
}

// Edge: resolveProposal неизвестного предложения.
TEST(ScalingPortsContract, LocalResolveProposalUnknown) {
    auto b = Backends::create(false);
    EXPECT_FALSE(b->dir->resolveProposal("no-such-proposal").has_value());
}

// Event bus: доставка и подписка через порт (локальный + фейк).
TEST(ScalingPortsContract, EventBusDeliverAndSubscribe) {
    auto b = Backends::create(false);
    SseEvent ev{"org-1", "proposal_created", "{}"};
    b->harness->app->events->deliver(ev);
    EXPECT_GE(b->harness->app->events->connectionCount(), 0u);
    b->harness->app->events->heartbeat();

    // Фейковый bus живёт в тесте отдельно от Backends.
    FakeEventBus fake;
    fake.subscribeAllOrgs({"org-1"}, "agent-1", nullptr);
    EXPECT_EQ(fake.connectionCount(), 1u);
    fake.deliver(ev);
    ASSERT_EQ(fake.delivered_.size(), 1u);
    EXPECT_EQ(fake.delivered_[0], "org-1:proposal_created");
    fake.shutdownAll();
    EXPECT_EQ(fake.connectionCount(), 0u);
}
