#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

using namespace voterpool;
using namespace voterpool::testing;

namespace {

Json::Value approveMemberArgs(const std::string& orgId, const std::string& targetId) {
    Json::Value args;
    args["org_id"] = orgId;
    args["title"] = "Approve membership";
    Json::Value action;
    action["kind"] = "APPROVE_MEMBER";
    Json::Value payload;
    payload["target_agent_id"] = targetId;
    action["payload"] = payload;
    args["action"] = action;
    return args;
}

}  // namespace

TEST(OnboardingFlow, FullClosedOrgJoinLifecycle) {
    auto h = Harness::create();
    AgentContext admin = h->registerAgent("of-admin");
    AgentContext candidate = h->registerAgent("of-candidate");

    Json::Value orgOut = createOrg(*h, admin, "Lifecycle Org", "CLOSED", orgConfigArgs("MAJORITY", 600));
    ASSERT_FALSE(h->isError(orgOut));
    std::string orgId = orgOut["org_id"].asString();

    // join -> PENDING.
    Json::Value joinArgs;
    joinArgs["org_id"] = orgId;
    Json::Value joined = h->call("join_organization", joinArgs, &candidate);
    ASSERT_FALSE(h->isError(joined)) << joined.toStyledString();
    EXPECT_EQ(joined["status"].asString(), "PENDING");

    // Заявка видна в списке (админ).
    Json::Value pendArgs;
    pendArgs["org_id"] = orgId;
    Json::Value pending = h->call("list_pending_members", pendArgs, &admin);
    ASSERT_EQ(pending.size(), 1u);
    EXPECT_EQ(pending[0]["agent_id"].asString(), candidate.agent_id);
    EXPECT_EQ(pending[0]["requested_at"].asInt64(), h->clock.nowSec());

    // Одобрение через консенсус.
    std::string pid =
        h->call("create_proposal", approveMemberArgs(orgId, candidate.agent_id), &admin)["proposal_id"]
            .asString();
    ASSERT_FALSE(pid.empty());
    Json::Value receipt = vote(*h, admin, pid, "YES");
    ASSERT_EQ(receipt["proposal_status"].asString(), "PASSED");

    // Кандидат активен и исчез из списка.
    auto membership = h->app->orgs->getMembership(orgId, candidate.agent_id);
    ASSERT_TRUE(membership.has_value());
    EXPECT_EQ(membership->status, MemberStatus::ACTIVE);
    EXPECT_EQ(h->call("list_pending_members", pendArgs, &admin).size(), 0u);

    // Идемпотентный повторный join не создаёт вторую запись.
    Json::Value again = h->call("join_organization", joinArgs, &candidate);
    EXPECT_EQ(again["status"].asString(), "ACTIVE");
    EXPECT_EQ(h->app->indexes->listPending(orgId).size(), 0u);
}

TEST(OnboardingFlow, TtlCloseUpdatesUpdatedAtAndFeedsUpdatedSince) {
    auto h = Harness::create();
    AgentContext admin = h->registerAgent("ttl-upd");
    Json::Value orgOut = createOrg(*h, admin, "Upd Org", "OPEN", orgConfigArgs("MAJORITY", 2));
    std::string orgId = orgOut["org_id"].asString();

    std::int64_t tCreate = h->clock.nowSec();
    std::string pid = createProposal(*h, admin, orgId, "upd")["proposal_id"].asString();
    auto before = h->app->proposals->get(orgId, pid);
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(before->updated_at, tCreate);

    // Закрытие по таймеру: updated_at = времени закрытия, в той же транзакции.
    h->clock.advanceSeconds(3);
    std::int64_t tClose = h->clock.nowSec();
    h->app->engine->closeExpired(tClose);
    auto after = h->app->proposals->get(orgId, pid);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->status, ProposalStatus::REJECTED);  // MAJORITY без голосов
    EXPECT_EQ(after->updated_at, tClose);

    // updated_since до закрытия -> предложение возвращается.
    Json::Value args;
    args["org_id"] = orgId;
    args["filter"] = "COMPLETED";
    args["updated_since"] = tCreate + 1;
    Json::Value items = h->call("get_proposals", args, &admin);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0]["proposal_id"].asString(), pid);
    EXPECT_EQ(items[0]["updated_at"].asInt64(), tClose);

    // updated_since после закрытия -> пусто.
    args["updated_since"] = tClose + 1;
    EXPECT_EQ(h->call("get_proposals", args, &admin).size(), 0u);
}

TEST(OnboardingFlow, EarlyConsensusCloseShiftsUpdatedAt) {
    auto h = Harness::create();
    AgentContext a1 = h->registerAgent("early-1");
    AgentContext a2 = h->registerAgent("early-2");
    Json::Value orgOut = createOrg(*h, a1, "Early Org", "OPEN", orgConfigArgs("MAJORITY", 600));
    std::string orgId = orgOut["org_id"].asString();

    Json::Value joinArgs;
    joinArgs["org_id"] = orgId;
    ASSERT_FALSE(h->isError(h->call("join_organization", joinArgs, &a2)));

    std::int64_t tCreate = h->clock.nowSec();
    std::string pid = createProposal(*h, a1, orgId, "early")["proposal_id"].asString();

    vote(*h, a1, pid, "YES");   // 50% - ещё не консенсус
    auto mid = h->app->proposals->get(orgId, pid);
    ASSERT_TRUE(mid.has_value());
    EXPECT_EQ(mid->status, ProposalStatus::ACTIVE);

    h->clock.advanceSeconds(4);
    std::int64_t tVote = h->clock.nowSec();
    Json::Value receipt = vote(*h, a2, pid, "YES");  // 100% -> PASSED досрочно
    ASSERT_EQ(receipt["proposal_status"].asString(), "PASSED");
    auto closed = h->app->proposals->get(orgId, pid);
    ASSERT_TRUE(closed.has_value());
    EXPECT_EQ(closed->updated_at, tVote);

    Json::Value args;
    args["org_id"] = orgId;
    args["filter"] = "COMPLETED";
    args["updated_since"] = tCreate + 1;
    Json::Value items = h->call("get_proposals", args, &a1);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0]["proposal_id"].asString(), pid);
}
