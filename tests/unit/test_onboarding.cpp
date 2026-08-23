#include "mcp/McpHandler.h"
#include "mcp/tools/ToolDefs.h"
#include "tests/common/Harness.h"
#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

using namespace voterpool;
using namespace voterpool::testing;

namespace {


Json::Value bareRoot() {
    Json::Value root;
    root["jsonrpc"] = "2.0";
    root["id"] = 1;
    root["method"] = "tools/call";
    return root;
}

// Всегда кладёт ключ io.voterpool/auth; пустой токен = пустой bearer.
Json::Value metaRoot(const std::string& token) {
    Json::Value root = bareRoot();
    root["params"]["_meta"]["io.voterpool/auth"]["bearer"] = token;
    return root;
}

}  // namespace

TEST(OnboardingUnit, MetaAuthMatrix) {
    auto h = Harness::create();
    AgentContext a = h->registerAgent("meta-agent");
    const std::string& token = Harness::ApiKeyStore::instance().get(a.agent_id);

    AgentContext out;
    // Нет _meta вообще -> Absent (запрос продолжается анонимно).
    EXPECT_EQ(mcp::authenticateViaMeta(*h->app, bareRoot(), out), mcp::MetaAuthStatus::Absent);
    // Нет params -> Absent.
    Json::Value noParams;
    noParams["jsonrpc"] = "2.0";
    EXPECT_EQ(mcp::authenticateViaMeta(*h->app, noParams, out), mcp::MetaAuthStatus::Absent);
    // Валидный токен -> Ok с правильным agent_id.
    EXPECT_EQ(mcp::authenticateViaMeta(*h->app, metaRoot(token), out), mcp::MetaAuthStatus::Ok);
    EXPECT_EQ(out.agent_id, a.agent_id);
    // Невалидный токен -> Invalid.
    EXPECT_EQ(mcp::authenticateViaMeta(*h->app, metaRoot("voterpool_sec_bogus"), out),
              mcp::MetaAuthStatus::Invalid);
    // Пустой bearer -> Invalid.
    EXPECT_EQ(mcp::authenticateViaMeta(*h->app, metaRoot(""), out), mcp::MetaAuthStatus::Invalid);
    // auth не объект -> Invalid.
    Json::Value malformed = metaRoot("");
    malformed["params"]["_meta"]["io.voterpool/auth"] = "voterpool_sec_x";
    EXPECT_EQ(mcp::authenticateViaMeta(*h->app, malformed, out), mcp::MetaAuthStatus::Invalid);
    // bearer не строка -> Invalid.
    Json::Value nonString = metaRoot("");
    nonString["params"]["_meta"]["io.voterpool/auth"]["bearer"] = 42;
    EXPECT_EQ(mcp::authenticateViaMeta(*h->app, nonString, out), mcp::MetaAuthStatus::Invalid);
}

TEST(OnboardingUnit, CatalogContainsNewToolsSorted) {
    auto h = Harness::create();
    const auto& cat = mcp::catalog();
    bool sawPlaybook = false, sawProposal = false, sawPending = false;
    std::string prev;
    for (const auto& def : cat) {
        std::string name = def.name;
        if (!prev.empty()) EXPECT_LT(prev, name) << "catalog must stay lexicographically sorted";
        prev = name;
        if (name == "get_playbook") sawPlaybook = true;
        if (name == "get_proposal") sawProposal = true;
        if (name == "list_pending_members") sawPending = true;
    }
    EXPECT_TRUE(sawPlaybook);
    EXPECT_TRUE(sawProposal);
    EXPECT_TRUE(sawPending);
}

TEST(OnboardingUnit, AnonymousFlagsInCatalog) {
    auto h = Harness::create();
    for (const auto& def : mcp::catalog()) {
        const bool anonymous = def.anonymous;
        if (std::string(def.name) == "register_agent") EXPECT_TRUE(anonymous);
        if (std::string(def.name) == "get_playbook") EXPECT_TRUE(anonymous);
        if (std::string(def.name) == "create_organization") EXPECT_FALSE(anonymous);
    }
}

TEST(OnboardingUnit, AnonymousEnforcementViaDispatch) {
    auto h = Harness::create();
    // Анонимный get_playbook работает без контекста агента.
    Json::Value pb = h->call("get_playbook", Json::Value(Json::objectValue), nullptr);
    ASSERT_FALSE(h->isError(pb));
    EXPECT_TRUE(pb.isMember("playbook"));
    EXPECT_GT(pb["playbook"].asString().size(), 200u);
    EXPECT_EQ(pb["full_guide"].asString(), "docs/14-agent-playbook.md");
    // Защищённый инструмент без контекста -> -32001.
    Json::Value args;
    args["name"] = "No Auth Org";
    Json::Value out = h->call("create_organization", args, nullptr);
    EXPECT_EQ(h->errorCode(out), -32001);
}

TEST(OnboardingUnit, GetProposalNotFoundAndIsolation) {
    auto h = Harness::create();
    AgentContext a = h->registerAgent("gp-agent");
    AgentContext outsider = h->registerAgent("gp-outsider");

    Json::Value unknown;
    unknown["proposal_id"] = "00000000-0000-4000-8000-000000000000";
    Json::Value miss = h->call("get_proposal", unknown, &a);
    EXPECT_EQ(h->errorCode(miss), -32004);

    Json::Value orgOut = createOrg(*h, a, "Gp Org", "CLOSED", orgConfigArgs("MAJORITY", 600));
    std::string orgId = orgOut["org_id"].asString();
    std::string pid = createProposal(*h, a, orgId, "gp")["proposal_id"].asString();

    Json::Value args;
    args["proposal_id"] = pid;
    Json::Value foreign = h->call("get_proposal", args, &outsider);
    EXPECT_EQ(h->errorCode(foreign), -32002);
}

TEST(OnboardingUnit, GetProposalFullCardWithVotes) {
    auto h = Harness::create();
    AgentContext admin = h->registerAgent("gp-admin");
    AgentContext voter = h->registerAgent("gp-voter");

    Json::Value orgOut = createOrg(*h, admin, "Gp Card Org", "OPEN", orgConfigArgs("MAJORITY", 600));
    std::string orgId = orgOut["org_id"].asString();
    Json::Value joinArgs;
    joinArgs["org_id"] = orgId;
    ASSERT_FALSE(h->isError(h->call("join_organization", joinArgs, &voter)));

    std::string pid = createProposal(*h, admin, orgId, "card")["proposal_id"].asString();
    vote(*h, voter, pid, "YES");

    Json::Value args;
    args["proposal_id"] = pid;
    Json::Value card = h->call("get_proposal", args, &admin);
    ASSERT_FALSE(h->isError(card)) << card.toStyledString();
    EXPECT_EQ(card["proposal_id"].asString(), pid);
    EXPECT_EQ(card["org_id"].asString(), orgId);
    EXPECT_EQ(card["status"].asString(), "ACTIVE");
    EXPECT_EQ(card["voters_count"].asInt64(), 1);
    EXPECT_DOUBLE_EQ(card["yes_power"].asDouble(), 1.0);
    EXPECT_TRUE(card.isMember("created_at"));
    EXPECT_TRUE(card.isMember("expires_at"));
    EXPECT_TRUE(card.isMember("updated_at"));
    EXPECT_EQ(card["action_applied"], Json::Value(Json::nullValue));
    EXPECT_FALSE(card["config_delta_applied"].asBool());

    const Json::Value& votes = card["votes"];
    ASSERT_EQ(votes.size(), 1u);
    EXPECT_EQ(votes[0]["agent_id"].asString(), voter.agent_id);
    EXPECT_EQ(votes[0]["decision"].asString(), "YES");
    EXPECT_DOUBLE_EQ(votes[0]["power_at_vote"].asDouble(), 1.0);
}

TEST(OnboardingUnit, UpdatedSinceValidationAndFiltering) {
    auto h = Harness::create();
    AgentContext admin = h->registerAgent("us-admin");
    Json::Value orgOut = createOrg(*h, admin, "Us Org", "OPEN", orgConfigArgs("MAJORITY", 600));
    std::string orgId = orgOut["org_id"].asString();
    std::int64_t t0 = h->clock.nowSec();
    std::string pid = createProposal(*h, admin, orgId, "us")["proposal_id"].asString();

    // Невалидные типы -> -32602.
    Json::Value bad;
    bad["org_id"] = orgId;
    bad["updated_since"] = "yesterday";
    EXPECT_EQ(h->errorCode(h->call("get_proposals", bad, &admin)), -32602);
    Json::Value negative = bad;
    negative["updated_since"] = -1;
    EXPECT_EQ(h->errorCode(h->call("get_proposals", negative, &admin)), -32602);
    Json::Value boolish = bad;
    boolish["updated_since"] = true;
    EXPECT_EQ(h->errorCode(h->call("get_proposals", boolish, &admin)), -32602);

    // updated_at присутствует в каждом элементе.
    Json::Value all;
    all["org_id"] = orgId;
    Json::Value items = h->call("get_proposals", all, &admin);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_TRUE(items[0].isMember("updated_at"));
    EXPECT_EQ(items[0]["proposal_id"].asString(), pid);

    // Строгий порог: updated_since = t0 исключает созданное ровно в t0;
    // t0-1 включает.
    Json::Value fresh = all;
    fresh["updated_since"] = t0;
    EXPECT_EQ(h->call("get_proposals", fresh, &admin).size(), 0u);
    Json::Value earlier = all;
    earlier["updated_since"] = t0 - 1;
    EXPECT_EQ(h->call("get_proposals", earlier, &admin).size(), 1u);

    // Голос сдвигает updated_at вперёд -> попадает в updated_since = t0+1.
    h->clock.advanceSeconds(2);
    vote(*h, admin, pid, "YES");
    Json::Value later = all;
    later["updated_since"] = t0 + 1;
    EXPECT_EQ(h->call("get_proposals", later, &admin).size(), 1u);
}

TEST(OnboardingUnit, ListPendingMembersRightsAndSorting) {
    auto h = Harness::create();
    AgentContext admin = h->registerAgent("lp-admin");
    AgentContext outsider = h->registerAgent("lp-outsider");

    Json::Value orgOut = createOrg(*h, admin, "Lp Org", "CLOSED", orgConfigArgs("MAJORITY", 600));
    std::string orgId = orgOut["org_id"].asString();

    Json::Value args;
    args["org_id"] = orgId;
    // Не-участник -> -32002 (ошибка приходит объектом, успех - массивом).
    Json::Value foreignPending = h->call("list_pending_members", args, &outsider);
    ASSERT_TRUE(foreignPending.isObject()) << "expected error object";
    EXPECT_EQ(foreignPending["__error__"].asInt(), -32002);

    // Три заявки в разное время.
    AgentContext c1 = h->registerAgent("lp-c1");
    AgentContext c2 = h->registerAgent("lp-c2");
    AgentContext c3 = h->registerAgent("lp-c3");
    ASSERT_TRUE(h->call("join_organization", args, &c2).isObject());
    h->clock.advanceSeconds(5);
    ASSERT_TRUE(h->call("join_organization", args, &c1).isObject());
    h->clock.advanceSeconds(5);
    ASSERT_TRUE(h->call("join_organization", args, &c3).isObject());

    Json::Value list = h->call("list_pending_members", args, &admin);
    ASSERT_TRUE(list.isArray()) << list.toStyledString();
    ASSERT_EQ(list.size(), 3u);
    EXPECT_EQ(list[0]["agent_id"].asString(), c2.agent_id);  // раньше всех
    EXPECT_EQ(list[1]["agent_id"].asString(), c1.agent_id);
    EXPECT_EQ(list[2]["agent_id"].asString(), c3.agent_id);
    EXPECT_TRUE(list[0].isMember("requested_at"));
    EXPECT_LT(list[0]["requested_at"].asInt64(), list[1]["requested_at"].asInt64());
}

TEST(OnboardingUnit, ApprovedCandidateLeavesPendingList) {
    auto h = Harness::create();
    AgentContext admin = h->registerAgent("ap-admin");
    AgentContext candidate = h->registerAgent("ap-candidate");

    Json::Value orgOut = createOrg(*h, admin, "Ap Org", "CLOSED", orgConfigArgs("MAJORITY", 600));
    std::string orgId = orgOut["org_id"].asString();
    Json::Value joinArgs;
    joinArgs["org_id"] = orgId;
    ASSERT_FALSE(h->isError(h->call("join_organization", joinArgs, &candidate)));

    // Одобрение через консенсус: ACTION APPROVE_MEMBER от админа, голосует сам (100% при SHARES? нет, EQUAL 1.0/1.0 = 100%).
    Json::Value pArgs;
    pArgs["org_id"] = orgId;
    pArgs["title"] = "Approve candidate";
    Json::Value action;
    action["kind"] = "APPROVE_MEMBER";
    Json::Value payload;
    payload["target_agent_id"] = candidate.agent_id;
    action["payload"] = payload;
    pArgs["action"] = action;
    Json::Value pOut = h->call("create_proposal", pArgs, &admin);
    ASSERT_FALSE(h->isError(pOut)) << pOut.toStyledString();
    std::string pid = pOut["proposal_id"].asString();
    Json::Value receipt = vote(*h, admin, pid, "YES");
    ASSERT_EQ(receipt["proposal_status"].asString(), "PASSED") << receipt.toStyledString();

    Json::Value args;
    args["org_id"] = orgId;
    Json::Value list = h->call("list_pending_members", args, &admin);
    EXPECT_EQ(list.size(), 0u) << "approved candidate must leave the pending list";

    // Идемпотентный повтор join: уже ACTIVE, не PENDING.
    Json::Value rejoined = h->call("join_organization", joinArgs, &candidate);
    EXPECT_EQ(rejoined["status"].asString(), "ACTIVE");
}
