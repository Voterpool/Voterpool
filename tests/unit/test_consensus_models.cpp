#include "consensus/IConsensusModel.h"

#include <gtest/gtest.h>

using namespace voterpool;

namespace {

Proposal makeProposal(ConsensusModel model, int quorum, double T, double y = 0, double n = 0,
                      double a = 0, std::int64_t voters = 0) {
    Proposal p;
    p.status = ProposalStatus::ACTIVE;
    p.config_at_creation.consensus_model = model;
    p.config_at_creation.quorum_percentage = quorum;
    p.total_voting_power_at_creation = T;
    p.yes_power = y;
    p.no_power = n;
    p.abstain_power = a;
    p.voters_count = voters;
    return p;
}

Proposal makeConsentProposal(double y = 0, double n = 0, double a = 0, std::int64_t voters = 0,
                             std::int64_t eligible = 0) {
    Proposal p = makeProposal(ConsensusModel::CONSENT, 0, static_cast<double>(eligible), y, n, a,
                              voters);
    p.eligible_voters_at_creation = eligible;
    return p;
}

}  // namespace

TEST(ConsensusMajority, PassesWhenMoreThanHalf) {
    MajorityModel m;
    auto r = m.evaluate(makeProposal(ConsensusModel::MAJORITY, 0, 100, 51), false);
    ASSERT_TRUE(r.finalStatus.has_value());
    EXPECT_EQ(*r.finalStatus, ProposalStatus::PASSED);
}

TEST(ConsensusMajority, ExactlyHalfDoesNotPass) {
    MajorityModel m;
    auto r = m.evaluate(makeProposal(ConsensusModel::MAJORITY, 0, 100, 50), true);
    EXPECT_EQ(*r.finalStatus, ProposalStatus::REJECTED);
}

TEST(ConsensusMajority, EarlyRejectAtHalfAgainst) {
    MajorityModel m;
    auto r = m.evaluate(makeProposal(ConsensusModel::MAJORITY, 0, 100, 10, 50), false);
    EXPECT_EQ(*r.finalStatus, ProposalStatus::REJECTED);
}

TEST(ConsensusMajority, ExpiredNeverBecomesExpiredStatus) {
    MajorityModel m;
    for (auto [y, n] : std::vector<std::pair<double, double>>{{0, 0}, {49, 49}, {30, 20}}) {
        auto r = m.evaluate(makeProposal(ConsensusModel::MAJORITY, 0, 100, y, n), true);
        ASSERT_TRUE(r.finalStatus.has_value());
        EXPECT_NE(*r.finalStatus, ProposalStatus::EXPIRED);
    }
}

TEST(ConsensusMajority, NonVotersCountAsAgainst) {
    MajorityModel m;
    auto r = m.evaluate(makeProposal(ConsensusModel::MAJORITY, 0, 100, 40), true);
    EXPECT_EQ(*r.finalStatus, ProposalStatus::REJECTED);
    auto stillAlive = m.evaluate(makeProposal(ConsensusModel::MAJORITY, 0, 100, 10), false);
    EXPECT_FALSE(stillAlive.finalStatus.has_value());
    auto early = m.evaluate(makeProposal(ConsensusModel::MAJORITY, 0, 100, 10, 50), false);
    EXPECT_EQ(*early.finalStatus, ProposalStatus::REJECTED);
}

TEST(ConsensusQuorum, PassesWithQuorumAndMajority) {
    QuorumModel m;
    auto r = m.evaluate(makeProposal(ConsensusModel::QUORUM_PERCENTAGE, 60, 100, 45, 15), false);
    EXPECT_EQ(*r.finalStatus, ProposalStatus::PASSED);
}

TEST(ConsensusQuorum, RejectsWhenNoMajorityDespiteQuorum) {
    QuorumModel m;
    auto r = m.evaluate(makeProposal(ConsensusModel::QUORUM_PERCENTAGE, 60, 100, 15, 45), false);
    EXPECT_EQ(*r.finalStatus, ProposalStatus::REJECTED);
}

TEST(ConsensusQuorum, StaysActiveWithoutQuorum) {
    QuorumModel m;
    auto r = m.evaluate(makeProposal(ConsensusModel::QUORUM_PERCENTAGE, 60, 100, 40, 10), false);
    EXPECT_FALSE(r.finalStatus.has_value());
}

TEST(ConsensusQuorum, ExpiresOnMissingQuorumAtDeadline) {
    QuorumModel m;
    auto r = m.evaluate(makeProposal(ConsensusModel::QUORUM_PERCENTAGE, 60, 100, 40, 10), true);
    EXPECT_EQ(*r.finalStatus, ProposalStatus::EXPIRED);
}

TEST(ConsensusQuorum, EarlyExitImpossiblePassExampleFromSpecs) {
    QuorumModel m;
    auto r = m.evaluate(makeProposal(ConsensusModel::QUORUM_PERCENTAGE, 80, 100, 0, 60), false);
    EXPECT_EQ(*r.finalStatus, ProposalStatus::REJECTED);
}

TEST(ConsensusQuorum, NoEarlyExitWhilePassStillPossible) {
    QuorumModel m;
    auto r = m.evaluate(makeProposal(ConsensusModel::QUORUM_PERCENTAGE, 80, 100, 0, 30), false);
    EXPECT_FALSE(r.finalStatus.has_value());
}

TEST(ConsensusQuorum, ThresholdScalesWithFrozenTotal) {
    QuorumModel m;
    auto r = m.evaluate(makeProposal(ConsensusModel::QUORUM_PERCENTAGE, 80, 70, 5, 0), false);
    EXPECT_FALSE(r.finalStatus.has_value());
    auto passable = m.evaluate(makeProposal(ConsensusModel::QUORUM_PERCENTAGE, 50, 100, 50, 0), false);
    EXPECT_EQ(*passable.finalStatus, ProposalStatus::PASSED);
    auto blocked = m.evaluate(makeProposal(ConsensusModel::QUORUM_PERCENTAGE, 51, 100, 50, 0), true);
    EXPECT_EQ(*blocked.finalStatus, ProposalStatus::EXPIRED);
}

TEST(ConsensusQuorum, FrozenTotalIgnoresNewMembers) {
    QuorumModel m;
    Proposal p = makeProposal(ConsensusModel::QUORUM_PERCENTAGE, 60, 100, 45, 15);
    p.total_voting_power_at_creation = 200;
    auto r = m.evaluate(p, false);
    EXPECT_FALSE(r.finalStatus.has_value());
}

TEST(ConsensusConsent, SingleYesDoesNotCloseWhileCircleIncomplete) {
    ConsentModel m;
    auto r = m.evaluate(makeConsentProposal(1.0, 0, 0, 1, 3), false);
    EXPECT_FALSE(r.finalStatus.has_value());
}

TEST(ConsensusConsent, FullCircleWithAbstainPassesOnLastVote) {
    ConsentModel m;
    auto partial = m.evaluate(makeConsentProposal(1.0, 0, 1.0, 2, 3), false);
    EXPECT_FALSE(partial.finalStatus.has_value());
    auto r = m.evaluate(makeConsentProposal(1.0, 0, 2.0, 3, 3), false);
    ASSERT_TRUE(r.finalStatus.has_value());
    EXPECT_EQ(*r.finalStatus, ProposalStatus::PASSED);
}

TEST(ConsensusConsent, AnyNoRejectsImmediately) {
    ConsentModel m;
    auto r = m.evaluate(makeConsentProposal(5, 0.5, 1, 4, 10), false);
    ASSERT_TRUE(r.finalStatus.has_value());
    EXPECT_EQ(*r.finalStatus, ProposalStatus::REJECTED);
    auto firstVoteNo = m.evaluate(makeConsentProposal(0, 1.0, 0, 1, 7), false);
    ASSERT_TRUE(firstVoteNo.finalStatus.has_value());
    EXPECT_EQ(*firstVoteNo.finalStatus, ProposalStatus::REJECTED);
}

TEST(ConsensusConsent, ExpiredWithPartialTurnoutAndYesIsExpired) {
    ConsentModel m;
    auto r = m.evaluate(makeConsentProposal(1.0, 0, 0, 1, 3), true);
    ASSERT_TRUE(r.finalStatus.has_value());
    EXPECT_EQ(*r.finalStatus, ProposalStatus::EXPIRED);
}

TEST(ConsensusConsent, AllAbstainedExpiresAtDeadline) {
    ConsentModel m;
    auto r = m.evaluate(makeConsentProposal(0, 0, 3, 3, 3), true);
    ASSERT_TRUE(r.finalStatus.has_value());
    EXPECT_EQ(*r.finalStatus, ProposalStatus::EXPIRED);
    auto stillActiveBeforeDeadline =
        m.evaluate(makeConsentProposal(0, 0, 3, 3, 3), false);
    EXPECT_FALSE(stillActiveBeforeDeadline.finalStatus.has_value());
}

TEST(ConsensusConsent, NoVotesAtDeadlineExpires) {
    ConsentModel m;
    auto r = m.evaluate(makeConsentProposal(), true);
    ASSERT_TRUE(r.finalStatus.has_value());
    EXPECT_EQ(*r.finalStatus, ProposalStatus::EXPIRED);
}

TEST(ConsensusConsent, SingleMemberCirclePassesImmediately) {
    ConsentModel m;
    auto r = m.evaluate(makeConsentProposal(1.0, 0, 0, 1, 1), false);
    ASSERT_TRUE(r.finalStatus.has_value());
    EXPECT_EQ(*r.finalStatus, ProposalStatus::PASSED);
}

TEST(ConsensusConsent, LegacyZeroEligibleKeepsOldBehavior) {
    ConsentModel m;
    auto r = m.evaluate(makeConsentProposal(1.0, 0, 0, 1, 0), false);
    ASSERT_TRUE(r.finalStatus.has_value());
    EXPECT_EQ(*r.finalStatus, ProposalStatus::PASSED);
}

TEST(ConsensusFactory, BuildsModelsAndValidatesDecisions) {
    EXPECT_EQ(makeConsensusModel(ConsensusModel::MAJORITY)->name(), std::string("MAJORITY"));
    EXPECT_EQ(makeConsensusModel(ConsensusModel::QUORUM_PERCENTAGE)->allowedDecisions().size(), 2u);
    EXPECT_EQ(makeConsensusModel(ConsensusModel::CONSENT)->allowedDecisions().size(), 3u);
    EXPECT_TRUE(makeConsensusModel(ConsensusModel::MAJORITY)->allowedDecisions().count(VoteDecision::ABSTAIN) == 0);
}
