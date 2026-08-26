// Интеграционные тесты мульти-бакетных сканов и метрики распределения.
//
// Организации размещаются принудительно в КРАЙНИХ бакетах (0, 127, 255)
// подбором id из замороженных тест-векторов + обычный середняк для контроля.
#include "common/Harness.h"

#include "scaling/BucketResolver.h"
#include "storage/Keys.h"

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <vector>

namespace {

using namespace voterpool;

struct BucketFixture {
    std::shared_ptr<voterpool::testing::Harness> h;
    std::vector<std::string> orgIds;    // порядок: бакеты 0, 127, 255, middle
    std::vector<std::int64_t> createdAt;
    AgentContext admin{};

    static Json::Value vectors() {
        std::ifstream in(std::string(VOTERPOOL_FIXTURES_DIR) + "/bucket_vectors.json");
        Json::Value root;
        Json::CharReaderBuilder rb;
        std::string errs;
        EXPECT_TRUE(Json::parseFromStream(rb, in, &root, &errs)) << errs;
        return root;
    }

    static std::string probeFor(const Json::Value& root, int bucket) {
        for (const auto& v : root["vectors"])
            if (v["bucket"].asInt() == bucket && v.isMember("boundary") && v["boundary"].asBool())
                return v["org_id"].asString();
        ADD_FAILURE() << "no boundary vector for bucket " << bucket;
        return {};
    }

    static BucketFixture create() {
        BucketFixture f;
        f.h = voterpool::testing::Harness::create();

        // Агент-администратор всех организаций.
        auto reg = mcp::dispatchToolForTests(
            *f.h->app, nullptr, "register_agent", [&] {
                Json::Value a;
                a["name"] = "Scan Admin";
                return a;
            }());
        EXPECT_TRUE(reg.ok());
        f.admin = AgentContext{reg.value()["agent_id"].asString(), "NATIVE", false};

        const Json::Value root = vectors();
        // Первые три — граничные бакеты; четвёртая орга — «середняк» с
        // произвольным бакетом (хэш от собственного id).
        const std::vector<int> boundaryBuckets = {0, 127, 255};
        const std::vector<std::string> names = {"scanorg alpha", "scanorg beta",
                                                "scanorg gamma", "scanorg delta"};
        std::int64_t ts = 1700001000;
        for (size_t i = 0; i < names.size(); ++i) {
            std::string orgId = i < 3 ? probeFor(root, boundaryBuckets[i])
                                      : std::string("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");

            Organization org;
            org.org_id = orgId;
            org.name = names[i];
            org.short_description = "scan fixture";
            org.type = OrgType::OPEN;
            org.status = OrgStatus::ACTIVE;
            org.created_at = ts;
            org.updated_at = ts;
            org.config.consensus_model = ConsensusModel::MAJORITY;
            org.config.voting_duration_sec = 50;
            org.config.power_distribution = PowerDistribution::EQUAL;
            org.total_voting_power = 1.0;
            if (i == 2) org.tags = Codec::tagsFromJson([&] {
                    Json::Value arr(Json::arrayValue);
                    arr.append("edge");
                    return arr;
                }());
            if (i == 1) org.category = "edgecat";

            // Сидирование напрямую через репозитории (ключи скорятся Keys.h);
            // реестр имён — через порт Directory (как делает create_org).
            rocksdb::WriteBatch batch;
            f.h->app->db->put(batch, "cf_organizations", Keys::org(orgId),
                              Codec::serializeOrg(org));
            f.h->app->indexes->addFeedActive(batch, org);
            f.h->app->indexes->setTags(batch, org);
            f.h->app->indexes->setCategory(batch, org);
            EXPECT_TRUE(f.h->app->db->commit(batch));
            f.h->app->directory->indexOrg(orgId, Keys::nameLower(names[i]));

            // Членство админа (пишет membership + agent_orgs атомарно).
            Membership m;
            m.org_id = orgId;
            m.agent_id = f.admin.agent_id;
            m.role = MemberRole::ADMIN;
            m.status = MemberStatus::ACTIVE;
            m.voting_power = 1.0;
            m.created_at = m.updated_at = ts;
            rocksdb::WriteBatch mb;
            f.h->app->identity->recordMembershipLink(mb, m);
            EXPECT_TRUE(f.h->app->db->commit(mb));

            f.orgIds.push_back(orgId);
            f.createdAt.push_back(ts);
            ++ts;
        }
        return f;
    }

    Json::Value search(const std::string& query) {
        Json::Value a;
        a["query"] = query;
        auto r = mcp::dispatchToolForTests(*h->app, &admin, "search_organizations", a);
        EXPECT_TRUE(r.ok());
        return r.ok() ? r.value() : Json::Value();
    }
};

}  // namespace

// 4.1: данные в крайних бакетах находятся лентой, поиском и «мои организации».
TEST(BucketScans, BoundaryBucketsAreVisible) {
    auto f = BucketFixture::create();

    // Лента ACTIVE: все четыре, порядок created_at DESC (глобальный).
    auto feed = f.h->app->directory->scanFeedActive();
    ASSERT_EQ(feed.size(), 4u);
    std::set<std::string> feedSet(feed.begin(), feed.end());
    for (const auto& id : f.orgIds) EXPECT_TRUE(feedSet.count(id));
    // Глобальный порядок: по убыванию created_at.
    for (size_t i = 0; i + 1 < feed.size(); ++i) {
        auto a = f.h->app->orgs->get(feed[i]);
        auto b = f.h->app->orgs->get(feed[i + 1]);
        ASSERT_TRUE(a && b);
        EXPECT_GE(a->created_at, b->created_at);
    }

    // Поиск по подстроке через реестр имён.
    Json::Value q;
    q["query"] = "scanorg";
    auto res = mcp::dispatchToolForTests(*f.h->app, &f.admin, "search_organizations", q);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(res.value()["items"].size(), 4u);

    // «Мои организации»: связи в разных бакетах, порядок org_id asc.
    auto mine = f.h->app->identity->listOrgsOfAgent(f.admin.agent_id);
    ASSERT_EQ(mine.size(), 4u);
    for (size_t i = 0; i + 1 < mine.size(); ++i) EXPECT_LT(mine[i].org_id, mine[i + 1].org_id);

    // Теги/категории в крайних бакетах.
    EXPECT_EQ(f.h->app->directory->scanTag("edge"), (std::set<std::string>{f.orgIds[2]}));
    EXPECT_EQ(f.h->app->directory->scanCategory("edgecat"),
              (std::set<std::string>{f.orgIds[1]}));
}

// 4.1+4.2: TTL-воркер закрывает истёкшие предложения во ВСЕХ бакетах;
// пагинация поиска курсором собирает всё без дублей и пропусков.
TEST(BucketScans, TtlClosesAcrossBucketsAndCursorPaginationComplete) {
    auto f = BucketFixture::create();

    // По предложению в каждой организации (истекают одновременно).
    std::vector<std::string> propIds;
    for (size_t i = 0; i < f.orgIds.size(); ++i) {
        Json::Value a;
        a["org_id"] = f.orgIds[i];
        a["title"] = "Expiry " + std::to_string(i);
        auto r = mcp::dispatchToolForTests(*f.h->app, &f.admin, "create_proposal", a);
        if (!r.ok())
            ADD_FAILURE() << "create_proposal[" << i << "] code=" << r.error().code
                          << " msg=" << r.error().message;
        ASSERT_TRUE(r.ok());
        propIds.push_back(r.value()["proposal_id"].asString());
    }

    // Пагинация ДО истечения: limit=2, два прохода, курсор пересекает бакеты.
    Json::Value page1;
    page1["query"] = "scanorg";
    page1["limit"] = 2;
    auto p1 = mcp::dispatchToolForTests(*f.h->app, &f.admin, "search_organizations", page1);
    ASSERT_TRUE(p1.ok());
    EXPECT_EQ(p1.value()["items"].size(), 2u);
    ASSERT_FALSE(p1.value()["next_cursor"].empty());

    Json::Value page2;
    page2["query"] = "scanorg";
    page2["limit"] = 2;
    page2["cursor"] = p1.value()["next_cursor"];
    auto p2 = mcp::dispatchToolForTests(*f.h->app, &f.admin, "search_organizations", page2);
    ASSERT_TRUE(p2.ok());
    std::set<std::string> seen;
    for (const auto& it : p1.value()["items"]) seen.insert(it["org_id"].asString());
    for (const auto& it : p2.value()["items"]) seen.insert(it["org_id"].asString());
    EXPECT_EQ(seen.size(), 4u);  // без дублей и пропусков

    // Истечение: тик TTL-воркера закрывает все четыре (бакеты 0/127/255/mid).
    f.h->clock.advanceSeconds(200);
    f.h->app->engine->closeExpired(f.h->clock.nowSec());

    for (size_t i = 0; i < propIds.size(); ++i) {
        Json::Value g;
        g["proposal_id"] = propIds[i];
        auto r =
            mcp::dispatchToolForTests(*f.h->app, &f.admin, "get_proposal", g);
        ASSERT_TRUE(r.ok()) << propIds[i];
        // MAJORITY без голосов закрывается REJECTED по таймеру
        // (неявка = против; EXPIRED для модели невозможна, docs/02 §1.2).
        EXPECT_EQ(r.value()["status"].asString(), "REJECTED")
            << "proposal in bucket-org " << f.orgIds[i];
    }
}

// 4.3: метрика voterpool_bucket_records — сумма по бакетам = числу орг-записей.
TEST(BucketScans, BucketHistogramMetricSumsToOrganizations) {
    auto f = BucketFixture::create();
    f.h->app->db->publishBucketHistogram();
    const std::string body = MetricsRegistry::instance().expose();

    // Парсим семплы gauge.
    std::int64_t total = 0;
    std::map<std::string, std::int64_t> perBucket;
    size_t pos = 0;
    while ((pos = body.find("voterpool_bucket_records{", pos)) != std::string::npos) {
        const size_t labelEnd = body.find('}', pos);
        const std::string labels = body.substr(pos + strlen("voterpool_bucket_records{"),
                                               labelEnd - pos - strlen("voterpool_bucket_records{"));
        const size_t eol = body.find('\n', labelEnd);
        const std::string line = body.substr(labelEnd, eol - labelEnd);
        const size_t space = line.rfind(' ');
        const std::int64_t val = std::stoll(line.substr(space + 1));
        perBucket[labels] = val;
        total += val;
        pos = labelEnd;
    }
    EXPECT_GE(perBucket.size(), 4u);          // как минимум 4 непустых бакета
    EXPECT_EQ(total, 4);                       // сидировали ровно 4 организации
    for (int b : {0, 127, 255}) {
        const std::string lbl = "bucket=\"" + scaling::bucketPrefix(b).substr(0, 4) + "\"";
        EXPECT_EQ(perBucket[lbl], 1) << lbl;   // ровно одна орга в каждом крайнем
    }
}
