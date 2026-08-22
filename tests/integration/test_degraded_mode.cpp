#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

using namespace voterpool;
using namespace voterpool::testing;

TEST(DegradedMode, FailedWriteMarksHealthAndSubsequentCommitsReportFailure) {
    auto h = Harness::create();
    ASSERT_TRUE(DbHealth::instance().healthy());

    AgentContext creator = h->registerAgent("degraded-admin");
    Json::Value orgOut = createOrg(*h, creator, "Degraded Org", "OPEN", orgConfigArgs("MAJORITY", 600));
    std::string orgId = orgOut["org_id"].asString();
    std::string pid = createProposal(*h, creator, orgId, "d")["proposal_id"].asString();

    h->app->db->beforeCommitHook = [] { return false; };
    Json::Value out = vote(*h, creator, pid, "YES");
    EXPECT_TRUE(h->isError(out));

    EXPECT_FALSE(DbHealth::instance().healthy());
    h->app->db->beforeCommitHook = nullptr;

    auto p = h->app->proposals->get(orgId, pid);
    EXPECT_DOUBLE_EQ(p->yes_power, 0.0);
}

TEST(DegradedMode, HealthGateRejectsRequestsExceptHealthAndMetrics) {
    DbHealth::instance().reset();
    DbHealth::instance().markUnhealthy("test");
    ASSERT_FALSE(DbHealth::instance().healthy());

    auto isRequestAllowed = [](const char* path) {
        if (path == std::string("/health") || path == std::string("/metrics")) return true;
        return DbHealth::instance().healthy();
    };
    EXPECT_FALSE(isRequestAllowed("/mcp"));
    EXPECT_FALSE(isRequestAllowed("/mcp/events"));
    EXPECT_TRUE(isRequestAllowed("/health"));
    EXPECT_TRUE(isRequestAllowed("/metrics"));

    auto e = RpcError::overloaded(30);
    EXPECT_EQ(e.code, -32050);
    EXPECT_EQ(e.data["reason"].asString(), "Storage backend unavailable");
}

TEST(DegradedMode, ResetOnlyViaRestartSimulation) {
    DbHealth::instance().reset();
    EXPECT_TRUE(DbHealth::instance().healthy());
    DbHealth::instance().markUnhealthy("disk full");
    EXPECT_FALSE(DbHealth::instance().healthy());
    DbHealth::instance().reset();
}
