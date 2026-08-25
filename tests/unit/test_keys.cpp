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
    EXPECT_EQ(Keys::category(Keys::nameLower("Governance"), "o"), std::string("category:governance:o"));
}

TEST(Keys, AuditKeyHasFixedWidthFields) {
    // Формат design D3: audit:{org}:{ms %020}:{salt %016x}{seq %019}
    std::string k = Keys::auditKey("o", 1697056500123, 0xdeadbeefULL, 5);
    EXPECT_EQ(k, "audit:o:00000001697056500123:00000000deadbeef0000000000000000005");
}

TEST(Keys, AuditKeysOfOneBootSortChronologicallyInsideSameMillisecond) {
    // Фиксированная ширина seq: лексикографический порядок ключей одного
    // запуска совпадает с порядком записи (раньше seq не паддировался).
    std::string prev = Keys::auditKey("o", 1697056500123, 0xAABBCCDDULL, 0);
    for (std::int64_t seq = 1; seq <= 1500; ++seq) {
        std::string k = Keys::auditKey("o", 1697056500123, 0xAABBCCDDULL, seq);
        ASSERT_LT(prev, k) << "seq " << seq;
        prev = k;
    }
    std::string later = Keys::auditKey("o", 1697056500124, 0xAABBCCDDULL, 0);
    EXPECT_LT(prev, later) << "later millisecond sorts after all of the previous one";
}

TEST(Keys, AuditPrefixScanIsIndependentOfTail) {
    // listByOrg сканирует префикс audit:{org}: — формат хвоста не важен,
    // старые записи с прежним коротким форматом остаются читаемыми.
    std::string modern = Keys::auditKey("org-1", 1697056500123, 1234567890123456789ULL, 42);
    const std::string legacy = "audit:org-1:0000001697056500123:7";
    for (const std::string& k : {modern, legacy}) {
        ASSERT_EQ(k.rfind("audit:org-1:", 0), 0u) << k;
    }
    EXPECT_NE(modern, legacy);
}

TEST(Keys, JoinLimitUsesUtcDayStamp) {
    std::int64_t ts = 1700000000;
    EXPECT_EQ(Keys::yyyymmdd(ts), "20231114");
    EXPECT_EQ(Keys::joinLimit("o1", ts), "join_limit:o1:20231114");
}
