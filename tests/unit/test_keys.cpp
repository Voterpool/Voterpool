#include "storage/Keys.h"

#include <gtest/gtest.h>

using namespace voterpool;

TEST(Keys, MembershipAndProposalKeysUseColonConcatenation) {
    EXPECT_EQ(Keys::membership("o1", "a1"), "org:o1:member:a1");
    EXPECT_EQ(Keys::proposal("o1", "p1"), "org:o1:proposal:p1");
    EXPECT_EQ(Keys::vote("o1", "p1", "a2"), "org:o1:proposal:p1:vote:a2");
    EXPECT_EQ(Keys::agentOrgs("a1", "o9"), "agent_orgs:a1:o9");
}

TEST(Keys, TimestampsAreZeroPaddedForLexicographicOrder) {
    std::string small = Keys::activeProposal(1000, "p");
    std::string large = Keys::activeProposal(20000000000, "p");
    ASSERT_EQ(small.size(), large.size());
    EXPECT_LT(small, large);
}

TEST(Keys, FeedUsesReversedTimestampNewestFirst) {
    std::string older = Keys::orgFeed("ACTIVE", 1700000000, "old");
    std::string newer = Keys::orgFeed("ACTIVE", 1799999999, "new");
    EXPECT_LT(newer, older);
}

TEST(Keys, TagsAndNamesAndCategoriesLowercased) {
    EXPECT_EQ(Keys::tagLower("InfRa"), "infra");
    EXPECT_EQ(Keys::nameLower("AI Council"), "ai council");
    EXPECT_EQ(Keys::tag("infra", "o"), "tag:infra:o");
    EXPECT_EQ(Keys::orgName("ai council", "o"), "org_name:ai council:o");
    EXPECT_EQ(Keys::category(Keys::nameLower("Governance"), "o"), std::string("category:governance:o"));
}

TEST(Keys, AuditKeysHaveMillisecondTimestampAndSeq) {
    std::string k1 = Keys::auditKey("o", 1697056500123, 5);
    std::string k2 = Keys::auditKey("o", 1697056500999, 1);
    EXPECT_NE(k1, k2);
    EXPECT_TRUE(k1.rfind("audit:o:", 0) == 0);
}

TEST(Keys, JoinLimitUsesUtcDayStamp) {
    std::int64_t ts = 1700000000;
    EXPECT_EQ(Keys::yyyymmdd(ts), "20231114");
    EXPECT_EQ(Keys::joinLimit("o1", ts), "join_limit:o1:20231114");
}
