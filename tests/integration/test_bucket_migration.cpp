// Интеграционные тесты миграции v2→v3: legacy-сид, идемпотентность,
// возобновление после сбоя коммита, dry-run.
//
// Legacy-база v2 собирается вручную (сырые ключи без префикса бакета),
// затем открывается через AppContext — SchemaManager выполняет миграцию.
#include "common/Harness.h"

#include "scaling/BucketResolver.h"
#include "storage/Codec.h"
#include "storage/Keys.h"
#include "storage/SchemaVersion.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

using namespace voterpool;

// ---- Хелперы legacy-сидирования (ключи СТАРОЙ раскладки, без 'bNNN:') ----

struct LegacySeed {
    std::string dir;
    std::unique_ptr<RocksDBWrapper> db;

    static LegacySeed create(const std::string& dirBase) {
        LegacySeed s;
        s.dir = dirBase;
        std::filesystem::create_directories(dirBase);
        s.db = std::make_unique<RocksDBWrapper>(makeCfg(dirBase));
        EXPECT_TRUE(s.db->open());
        return s;
    }

    static StorageConfig makeCfg(const std::string& path) {
        StorageConfig c;
        c.path = path;
        return c;
    }

    void put(const char* cf, const std::string& k, const std::string& v) {
        rocksdb::WriteBatch b;
        db->put(b, cf, k, v);
        ASSERT_TRUE(db->commit(b));
    }

    void setVersion2() { put("default", "meta:schema_version", "2"); }

    // Полная перепись базы: "cf \x01 key \x02 value" построчно.
    std::string dump() {
        std::ostringstream out;
        for (const char* cf : RocksDBWrapper::kCfNames) {
            auto it = db->newIterator(cf);
            for (it->SeekToFirst(); it->Valid(); it->Next()) {
                out << cf << '\x01' << it->key().ToString() << '\x02'
                    << it->value().ToString() << '\n';
            }
        }
        return out.str();
    }
};

// Канонический legacy-набор: 2 организации, членства, предложения
// (активное + закрытое), голос, полный набор индексов.
void seedCanonical(LegacySeed& s) {
    const char* ORG_A = "11111111-1111-4111-8111-111111111111";
    const char* ORG_B = "22222222-2222-4222-8222-222222222222";
    Agent admin;
    admin.agent_id = "aaaaaaaa-0000-4000-8000-000000000001";
    admin.name = "Legacy Admin";
    admin.api_key_hash = "hash-admin";
    admin.created_at = admin.updated_at = 1700000000;
    Agent member;
    member.agent_id = "bbbbbbbb-0000-4000-8000-000000000002";
    member.name = "Legacy Member";
    member.api_key_hash = "hash-member";
    member.created_at = member.updated_at = 1700000000;
    s.put("default", Keys::agent(admin.agent_id), Codec::serializeAgent(admin));
    s.put("default", Keys::agent(member.agent_id), Codec::serializeAgent(member));
    s.put("cf_auth", Keys::auth("hash-admin"), admin.agent_id);
    s.put("cf_auth", Keys::auth("hash-member"), member.agent_id);

    Organization a;
    a.org_id = ORG_A;
    a.name = "Alpha Org";
    a.short_description = "first";
    a.type = OrgType::OPEN;
    a.status = OrgStatus::ACTIVE;
    a.created_at = 1700000100;
    a.updated_at = 1700000100;
    a.config.consensus_model = ConsensusModel::MAJORITY;
    a.config.voting_duration_sec = 1000;
    a.config.power_distribution = PowerDistribution::EQUAL;
    a.total_voting_power = 2.0;
    s.put("cf_organizations", std::string("org:") + a.org_id, Codec::serializeOrg(a));

    Organization b = a;
    b.org_id = ORG_B;
    b.name = "Beta Org";
    b.short_description = "second";
    b.created_at = 1700000200;
    b.updated_at = 1700000200;
    b.category = "research";
    s.put("cf_organizations", std::string("org:") + b.org_id, Codec::serializeOrg(b));

    Membership m1;
    m1.org_id = ORG_A;
    m1.agent_id = admin.agent_id;
    m1.role = MemberRole::ADMIN;
    m1.status = MemberStatus::ACTIVE;
    m1.voting_power = 1.0;
    m1.created_at = m1.updated_at = 1700000105;
    Membership m2 = m1;
    m2.agent_id = member.agent_id;
    m2.role = MemberRole::MEMBER;
    s.put("cf_memberships", std::string("org:") + ORG_A + ":member:" + m1.agent_id,
          Codec::serializeMembership(m1));
    s.put("cf_memberships", std::string("org:") + ORG_A + ":member:" + m2.agent_id,
          Codec::serializeMembership(m2));
    s.put("cf_agent_orgs", "agent_orgs:" + m1.agent_id + ":" + ORG_A, "ADMIN/ACTIVE");
    s.put("cf_agent_orgs", "agent_orgs:" + m2.agent_id + ":" + ORG_A, "MEMBER/ACTIVE");

    Proposal active;
    active.proposal_id = "33333333-3333-4333-8333-333333333301";
    active.org_id = ORG_A;
    active.creator_id = admin.agent_id;
    active.title = "Legacy active";
    active.description = "body";
    active.status = ProposalStatus::ACTIVE;
    active.created_at = 1700000300;
    active.expires_at = 1799999300;
    active.config_at_creation.consensus_model = ConsensusModel::MAJORITY;
    active.config_at_creation.voting_duration_sec = 1000;
    active.config_at_creation.power_distribution = PowerDistribution::EQUAL;
    active.eligible_voters_at_creation = 2;
    Proposal closed = active;
    closed.proposal_id = "33333333-3333-4333-8333-333333333302";
    closed.status = ProposalStatus::PASSED;
    closed.yes_power = 2.0;
    closed.voters_count = 2;
    s.put("cf_proposals", std::string("org:") + ORG_A + ":proposal:" + active.proposal_id,
          Codec::serializeProposal(active));
    s.put("cf_proposals", std::string("org:") + ORG_A + ":proposal:" + closed.proposal_id,
          Codec::serializeProposal(closed));

    Vote v;
    v.proposal_id = closed.proposal_id;
    v.agent_id = admin.agent_id;
    v.decision = VoteDecision::YES;
    v.power_at_vote = 1.0;
    v.created_at = 1700000400;
    s.put("cf_votes",
          std::string("org:") + ORG_A + ":proposal:" + closed.proposal_id + ":vote:" + v.agent_id,
          Codec::serializeVote(v));

    // Индексы всех видов; owner org в значении active_proposals.
    s.put("cf_indexes", "active_proposals:" + Keys::padTs(active.expires_at) + ":" +
                            active.proposal_id, ORG_A);
    s.put("cf_indexes", "proposal_lookup:" + active.proposal_id, ORG_A);
    s.put("cf_indexes", "org_feed:ACTIVE:" + Keys::padTs(Keys::reverseTs(a.created_at)) + ":" + a.org_id, "");
    s.put("cf_indexes", "org_feed:ACTIVE:" + Keys::padTs(Keys::reverseTs(b.created_at)) + ":" + b.org_id, "");
    s.put("cf_indexes", std::string("tag:infra:") + ORG_A, "");
    s.put("cf_indexes", std::string("category:research:") + ORG_B, "");
    s.put("cf_indexes", std::string("join_limit:") + ORG_A + ":20261101", "3");
    s.put("cf_indexes", std::string("pending:") + ORG_B + ":cccccccc-0000-4000-8000-000000000003",
          "1700000500");

    s.setVersion2();
}

// ---- Утилиты тестов ----

static std::unique_ptr<AppContext> openAppAt(const std::string& dir) {
    DbHealth::instance().reset();
    AppConfig cfg;
    cfg.storage.path = dir;
    auto app = std::make_unique<AppContext>();
    app->config = cfg;
    static voterpool::MockClock clk(1700001000);
    app->init(&clk);
    return app;
}

static std::string dumpDir(const std::string& dir) {
    LegacySeed d;
    d.db = std::make_unique<RocksDBWrapper>(LegacySeed::makeCfg(dir));
    EXPECT_TRUE(d.db->open());
    std::string out = d.dump();
    d.db->close();
    return out;
}

static Json::Value callTool(AppContext& app, const AgentContext* agent,
                            const std::string& tool, Json::Value args) {
    auto r = mcp::dispatchToolForTests(app, agent, tool, args);
    Json::Value out;
    out["ok"] = r.ok();
    if (r.ok()) out["value"] = r.value();
    else { out["code"] = r.error().code; }
    return out;
}

// ---- T1: населённая legacy-база мигрирует, записи читаемы, легаси-ключей нет ----

TEST(BucketMigration, MigratesPopulatedDatabaseAndPreservesRecords) {
    auto dir = voterpool::testing::tempDbDir();
    {
        LegacySeed s = LegacySeed::create(dir);
        seedCanonical(s);
        s.db->close();
    }
    auto app = openAppAt(dir);

    // Версия схемы = 3.
    EXPECT_EQ(app->db->get("default", kSchemaVersionKey).value_or(""), "3");

    // Данные читаются инструментами через порты.
    AgentContext admin{"aaaaaaaa-0000-4000-8000-000000000001", "NATIVE", false};
    auto search = callTool(*app, &admin, "search_organizations", [&]{
        Json::Value a; a["query"] = "org"; return a; }());
    ASSERT_EQ(search["ok"].asBool(), true);
    // Обе организации найдены (лента ACTIVE из двух бакетов).
    EXPECT_EQ(search["value"]["items"].size(), 2u);

    auto orgView = callTool(*app, &admin, "get_proposals", [&]{
        Json::Value a; a["org_id"] = "11111111-1111-4111-8111-111111111111"; return a; }());
    EXPECT_EQ(orgView["ok"].asBool(), true);

    // Ни одного неквотированного legacy-ключа в орг-семействах.
    for (const char* cf : {"cf_organizations", "cf_memberships", "cf_proposals",
                           "cf_votes", "cf_agent_orgs"}) {
        auto it = app->db->newIterator(cf);
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            EXPECT_EQ(it->key().ToString()[0], 'b') << cf << "/" << it->key().ToString();
        }
    }
    // cf_indexes: либо 'b', либо системный proposal_lookup.
    {
        auto it = app->db->newIterator("cf_indexes");
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            const std::string k = it->key().ToString();
            ASSERT_TRUE(k[0] == 'b' || k.rfind("proposal_lookup:", 0) == 0) << k;
        }
    }
    // Чекпоинт-маркер удалён по завершении.
    EXPECT_FALSE(app->db->get("default", kMigration3CheckpointKey).has_value());

    // TTL-тик не трогает активное предложение (индекс перенесён корректно).
    app->engine->closeExpired(1700002000);
    auto stillActive = callTool(*app, &admin, "get_proposal",
                                [&]{ Json::Value a; a["proposal_id"] =
                                     "33333333-3333-4333-8333-333333333301"; return a; }());
    EXPECT_EQ(stillActive["value"]["status"].asString(), "ACTIVE");
}

// ---- T2: повторное открытие мигрированной базы — no-op ----

TEST(BucketMigration, ReopenIsNoOp) {
    auto dir = voterpool::testing::tempDbDir();
    {
        LegacySeed s = LegacySeed::create(dir);
        seedCanonical(s);
        s.db->close();
    }
    {
        auto app = openAppAt(dir);
    }
    const std::string dumpBefore = dumpDir(dir);
    {
        auto app = openAppAt(dir);
    }
    EXPECT_EQ(dumpDir(dir), dumpBefore);
}

// ---- T3: детерминированный сбой коммита → докат с чекпоинта без дублей ----

TEST(BucketMigration, ResumeFromCheckpointAfterFailedCommit) {
    auto dirFail = voterpool::testing::tempDbDir();
    auto dirRef = voterpool::testing::tempDbDir();
    for (const auto* d : {&dirFail, &dirRef}) {
        LegacySeed s = LegacySeed::create(*d);
        seedCanonical(s);
        s.db->close();
    }

    // Эталон: полная миграция идентичного сида.
    {
        auto app = openAppAt(dirRef);
    }
    const std::string referenceDump = dumpDir(dirRef);

    // Сбой: третий коммит миграции возвращает false (beforeCommitHook).
    {
        RocksDBWrapper db(LegacySeed::makeCfg(dirFail));
        ASSERT_TRUE(db.open());
        // Коммит #1 — перенос данных (успех), коммит #2 — writeVersion (сбой):
        // состояние «данные в v3-раскладке, версия всё ещё 2», маркер не
        // сохранён (он был в сорванном финальном батче).
        int commits = 0;
        db.beforeCommitHook = [&commits] { return ++commits > 1 ? false : true; };
        SchemaManager mgr(db);
        EXPECT_EQ(mgr.run(), SchemaGateResult::kError);
        EXPECT_EQ(db.get("default", kSchemaVersionKey).value_or(""), "2")
            << "версия не должна повышаться при неудачном завершении";
        db.beforeCommitHook = nullptr;
        db.close();
    }
    // Докат: второй запуск завершает миграцию.
    {
        RocksDBWrapper db(LegacySeed::makeCfg(dirFail));
        ASSERT_TRUE(db.open());
        SchemaManager mgr(db);
        // Повторный проход обязан быть безопасным: dest==src не удаляет записи.
        const auto gate = mgr.run();
        EXPECT_TRUE(gate == SchemaGateResult::kUpToDate || gate == SchemaGateResult::kMigrated)
            << "gate=" << static_cast<int>(gate);
        EXPECT_EQ(mgr.dbVersion(), VOTERPOOL_SCHEMA_VERSION);
    }
    EXPECT_EQ(dumpDir(dirFail), referenceDump);
}

// ---- T4: dry-run не изменяет базу и репортит объём ----

TEST(BucketMigration, DryRunLeavesDatabaseUntouchedAndReports) {
    auto dir = voterpool::testing::tempDbDir();
    {
        LegacySeed s = LegacySeed::create(dir);
        seedCanonical(s);
        s.db->close();
    }
    const std::string before = dumpDir(dir);

    RocksDBWrapper db(LegacySeed::makeCfg(dir));
    ASSERT_TRUE(db.open());
    SchemaManager mgr(db, /*dryRun=*/true);
    EXPECT_EQ(mgr.run(), SchemaGateResult::kUpToDate);
    std::int64_t total = 0;
    for (const auto& [cf, n] : mgr.dryRunReport()) total += n;
    EXPECT_GT(total, 0) << "dry-run должен репортить переносимые записи";
    EXPECT_EQ(mgr.dryRunReport().size(), 6u);  // шесть затронутых CF
    db.close();

    EXPECT_EQ(dumpDir(dir), before) << "dry-run изменил базу";
}
}  // namespace
