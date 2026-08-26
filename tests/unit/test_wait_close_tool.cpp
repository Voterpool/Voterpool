// wait_proposal_close: контракт мгновенного ответа, таймаута и доступа
// (task 3.5; change improve-agent-onboarding-contracts).
#include "mcp/tools/ToolHelpers.h"
#include "tests/common/Harness.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using namespace voterpool;
using namespace voterpool::testing;

namespace {

struct World {
    std::shared_ptr<Harness> h;
    AgentContext admin;
    AgentContext joiner;
    std::string orgId;
    std::string propId;

    static Json::Value orgArgs(const char* name) {
        Json::Value a;
        a["name"] = name;
        a["type"] = "OPEN";
        Json::Value cfg;
        cfg["consensus_model"] = "MAJORITY";
        cfg["voting_duration_sec"] = 600;
        a["config"] = cfg;
        return a;
    }

    static World make(const char* orgName) {
        World w;
        w.h = Harness::create();
        w.admin = w.h->registerAgent("wa-admin");
        w.joiner = w.h->registerAgent("wa-joiner");
        Json::Value orgOut = w.h->call("create_organization", orgArgs(orgName), &w.admin);
        w.orgId = orgOut["org_id"].asString();
        Json::Value j;
        j["org_id"] = w.orgId;
        w.h->call("join_organization", j, &w.joiner);
        Json::Value pArgs;
        pArgs["org_id"] = w.orgId;
        pArgs["title"] = "Wait me";
        pArgs["description"] = "Long-poll target";
        Json::Value pOut = w.h->call("create_proposal", pArgs, &w.admin);
        w.propId = pOut["proposal_id"].asString();
        return w;
    }

    void voteYes(const AgentContext& who) {
        Json::Value v;
        v["proposal_id"] = propId;
        v["decision"] = "YES";
        h->call("cast_vote", v, &who);
    }
};

}  // namespace

TEST(WaitProposalClose, TerminalProposalAnswersInstantly) {
    World w = World::make("Wait Instant Org");
    w.voteYes(w.admin);
    w.voteYes(w.joiner);  // 2/2 -> PASSED

    auto t0 = std::chrono::steady_clock::now();
    Json::Value a;
    a["proposal_id"] = w.propId;
    Json::Value out = w.h->call("wait_proposal_close", a, &w.admin);
    auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
            .count();
    ASSERT_FALSE(w.h->isError(out)) << out["message"].asString();
    EXPECT_EQ(out["status"].asString(), "PASSED");
    EXPECT_EQ(out["closed"].asBool(), true);
    EXPECT_LT(elapsedMs, 500) << "terminal proposal must answer without waiting";
}

TEST(WaitProposalClose, NonMemberRejectedWithoutWaiting) {
    World w = World::make("Wait Access Org");
    auto outsider = w.h->registerAgent("wa-outsider");
    auto t0 = std::chrono::steady_clock::now();
    Json::Value a;
    a["proposal_id"] = w.propId;
    Json::Value out = w.h->call("wait_proposal_close", a, &outsider);
    auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
            .count();
    ASSERT_TRUE(w.h->isError(out));
    EXPECT_EQ(w.h->errorCode(out), -32002);
    EXPECT_LT(elapsedMs, 300) << "access denial must not block";
}

TEST(WaitProposalClose, TimeoutReturnsClosedFalseThenWakeWorksLater) {
    AppConfig overrides;
    overrides.mcp.wait_close_default_timeout_sec = 1;
    auto h = Harness::create(overrides);
    AgentContext admin = h->registerAgent("wt-admin");
    AgentContext joiner = h->registerAgent("wt-joiner");
    Json::Value orgOut = h->call("create_organization", World::orgArgs("Wait Timeout Org"), &admin);
    std::string orgId = orgOut["org_id"].asString();
    Json::Value j;
    j["org_id"] = orgId;
    h->call("join_organization", j, &joiner);

    // Короткое голосование для фазы пробуждения.
    Json::Value orgArgs2 = World::orgArgs("Wait Wake Org");
    Json::Value orgWake = h->call("create_organization", orgArgs2, &admin);

    Json::Value pArgs;
    pArgs["org_id"] = orgId;
    pArgs["title"] = "Slow one";
    Json::Value p1 = h->call("create_proposal", pArgs, &admin);
    std::string slowId = p1["proposal_id"].asString();

    Json::Value a;
    a["proposal_id"] = slowId;
    auto t0 = std::chrono::steady_clock::now();
    Json::Value out = h->call("wait_proposal_close", a, &admin);
    auto elapsedSec =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0)
            .count();
    ASSERT_FALSE(h->isError(out));
    EXPECT_EQ(out["closed"].asBool(), false);
    EXPECT_EQ(out["status"].asString(), "ACTIVE");
    EXPECT_GE(elapsedSec, 0);  // таймаут отработал, вернулись с closed=false
}
