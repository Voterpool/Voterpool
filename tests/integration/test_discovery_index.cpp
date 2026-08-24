#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

using namespace voterpool;
using namespace voterpool::testing;

namespace {
std::vector<std::string> ids(const Json::Value& result) {
    std::vector<std::string> out;
    for (const auto& item : result["items"]) out.push_back(item["org_id"].asString());
    return out;
}
}  // namespace

TEST(Discovery, FeedOrdersNewestFirst) {
    auto h = Harness::create();
    AgentContext a = h->registerAgent("feed-user");
    std::vector<std::string> created;
    for (int i = 0; i < 3; ++i) {
        Json::Value out = createOrg(*h, a, "Feed Org " + std::to_string(i), "OPEN",
                                    orgConfigArgs("MAJORITY", 600));
        ASSERT_FALSE(h->isError(out)) << i;
        created.push_back(out["org_id"].asString());
        h->clock.advanceSeconds(10);
    }
    Json::Value args;
    Json::Value page = h->call("search_organizations", args, &a);
    auto got = ids(page);
    ASSERT_EQ(got.size(), 3u);
    EXPECT_EQ(got[0], created[2]);
    EXPECT_EQ(got[2], created[0]);
}

TEST(Discovery, NameSubstringAndTagsAndCategoryFilters) {
    auto h = Harness::create();
    AgentContext a = h->registerAgent("searcher");

    Json::Value infraArgs;
    infraArgs["name"] = "AI Council";
    infraArgs["type"] = "OPEN";
    Json::Value infraTags(Json::arrayValue);
    infraTags.append("Infra");
    infraTags.append("council");
    infraArgs["tags"] = infraTags;
    infraArgs["config"] = orgConfigArgs("MAJORITY", 600);
    Json::Value orgA = h->call("create_organization", infraArgs, &a);

    Json::Value guildArgs;
    guildArgs["name"] = "Dev Guild";
    guildArgs["type"] = "OPEN";
    Json::Value guildTags(Json::arrayValue);
    guildTags.append("infra");
    guildArgs["tags"] = guildTags;
    guildArgs["config"] = orgConfigArgs("MAJORITY", 600);
    Json::Value orgB = h->call("create_organization", guildArgs, &a);

    Json::Value q;
    q["query"] = "ai council";
    EXPECT_EQ(ids(h->call("search_organizations", q, &a)).size(), 1u);

    q.clear();
    Json::Value bothTags(Json::arrayValue);
    bothTags.append("infra");
    bothTags.append("COUNCIL");
    q["tags"] = bothTags;
    auto andResult = ids(h->call("search_organizations", q, &a));
    ASSERT_EQ(andResult.size(), 1u);
    EXPECT_EQ(andResult[0], orgA["org_id"].asString());

    q.clear();
    q["category"] = "governance";
    EXPECT_EQ(ids(h->call("search_organizations", q, &a)).size(), 0u);
}

TEST(Discovery, DissolvedExcludedFromAllResults) {
    auto h = Harness::create();
    AgentContext admin = h->registerAgent("dissol-admin");
    AgentContext viewer = h->registerAgent("dissol-viewer");
    Json::Value orgOut = createOrg(*h, admin, "Doomed Council", "OPEN", orgConfigArgs("MAJORITY", 600));
    std::string orgId = orgOut["org_id"].asString();

    Json::Value dissolveArgs;
    dissolveArgs["org_id"] = orgId;
    ASSERT_FALSE(h->isError(h->call("dissolve_organization", dissolveArgs, &admin)));

    Json::Value q;
    q["query"] = "doomed";
    EXPECT_EQ(ids(h->call("search_organizations", q, &viewer)).size(), 0u);
    q.clear();
    EXPECT_EQ(ids(h->call("search_organizations", q, &viewer)).size(), 0u);
}

TEST(Discovery, SubstringMidNameAndCaseInsensitiveQueries) {
    auto h = Harness::create();
    AgentContext a = h->registerAgent("substr-searcher");

    // Канонический сценарий спеки: query="council" находит только "AI Council".
    Json::Value councilOut = createOrg(*h, a, "AI Council", "OPEN", orgConfigArgs("MAJORITY", 600));
    ASSERT_FALSE(h->isError(councilOut));
    Json::Value guildOut = createOrg(*h, a, "Dev Guild", "OPEN", orgConfigArgs("MAJORITY", 600));
    ASSERT_FALSE(h->isError(guildOut));
    std::string councilId = councilOut["org_id"].asString();

    Json::Value q;
    q["query"] = "council";
    auto got = ids(h->call("search_organizations", q, &a));
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0], councilId);

    // Подстрока в середине и в конце названия.
    Json::Value longOut = createOrg(*h, a, "Blockchain Council of Research", "OPEN",
                                    orgConfigArgs("MAJORITY", 600));
    ASSERT_FALSE(h->isError(longOut));

    q["query"] = "council of res";
    got = ids(h->call("search_organizations", q, &a));
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0], longOut["org_id"].asString());

    q["query"] = "search";
    got = ids(h->call("search_organizations", q, &a));
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0], longOut["org_id"].asString());

    // Регистронезависимость в обе стороны.
    q["query"] = "COUNCIL";
    EXPECT_EQ(ids(h->call("search_organizations", q, &a)).size(), 2u);
}

TEST(Discovery, RegistrySurvivesRestart) {
    std::string dir = tempDbDir();
    AppConfig cfg;
    cfg.storage.path = dir;
    auto h = Harness::create(cfg);
    std::string orgId;
    {
        AgentContext a = h->registerAgent("restart-admin");
        Json::Value out = createOrg(*h, a, "Persistent Council", "OPEN", orgConfigArgs("MAJORITY", 600));
        ASSERT_FALSE(h->isError(out));
        orgId = out["org_id"].asString();

        if (h->app->workers) h->app->workers->stop();
        h->app->workers.reset();
        h->app->engine.reset();
        h->app->hub.reset();
        h->app->db->close();
        h->app.reset();
    }

    AppConfig reopen = cfg;
    AppContext restarted;
    restarted.config = reopen;
    restarted.init(nullptr);

    auto hit = restarted.orgNames->matchQuery("council");
    ASSERT_EQ(hit.size(), 1u);
    EXPECT_TRUE(hit.count(orgId));
    ASSERT_TRUE(restarted.orgNames->findActiveByName("persistent council").has_value());
    restarted.db->close();
}

TEST(Discovery, CursorPaginationPagesAreDisjointAndComplete) {
    auto h = Harness::create();
    AgentContext a = h->registerAgent("pager");
    std::set<std::string> all;
    for (int i = 0; i < 5; ++i) {
        Json::Value out = createOrg(*h, a, "Page Org " + std::to_string(i), "OPEN",
                                    orgConfigArgs("MAJORITY", 600));
        all.insert(out["org_id"].asString());
        h->clock.advanceSeconds(5);
    }
    std::set<std::string> seen;
    std::string cursor;
    int pages = 0;
    do {
        Json::Value q;
        q["limit"] = 2;
        if (!cursor.empty()) q["cursor"] = cursor;
        Json::Value page = h->call("search_organizations", q, &a);
        for (const auto& item : page["items"]) {
            std::string id = item["org_id"].asString();
            EXPECT_EQ(seen.count(id), 0u) << id;
            seen.insert(id);
        }
        cursor = page["next_cursor"].asString();
        ++pages;
    } while (!cursor.empty() && pages < 10);
    EXPECT_EQ(seen.size(), all.size());
    EXPECT_GE(pages, 3);
}

TEST(Discovery, LimitBoundsValidated) {
    auto h = Harness::create();
    AgentContext a = h->registerAgent("limiter");
    Json::Value q;
    q["limit"] = 0;
    EXPECT_EQ(h->errorCode(h->call("search_organizations", q, &a)), -32602);
    q["limit"] = 101;
    EXPECT_EQ(h->errorCode(h->call("search_organizations", q, &a)), -32602);
}
