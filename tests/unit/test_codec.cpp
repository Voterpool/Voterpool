#include "domain/Proposal.h"
#include "storage/Codec.h"

#include <gtest/gtest.h>

using namespace voterpool;

namespace {

Proposal sampleProposal() {
    Proposal p;
    p.proposal_id = "11111111-1111-4111-8111-111111111111";
    p.org_id = "22222222-2222-4222-8222-222222222222";
    p.creator_id = "33333333-3333-4333-8333-333333333333";
    p.title = "flags roundtrip";
    p.status = ProposalStatus::PASSED;
    p.created_at = 100;
    p.expires_at = 200;
    p.updated_at = 150;
    return p;
}

}  // namespace

// Целевое поведение (design D4): флаги исхода переживают roundtrip.
TEST(ProposalFlags, RoundTripPreservesAppliedFlags) {
    Proposal p = sampleProposal();
    p.action_applied = true;
    p.config_delta_applied = true;

    auto restored = Codec::deserializeProposal(Codec::serializeProposal(p));
    ASSERT_TRUE(restored.has_value());
    EXPECT_TRUE(restored->action_applied);
    EXPECT_TRUE(restored->config_delta_applied);
}

TEST(ProposalFlags, MissingFieldsDeserializeAsFalseNoDerivation) {
    // Запись без полей (например, от старого бинарника): false, без
    // деривации из статуса PASSED / наличия action или config_delta.
    Json::Value v;
    v["proposal_id"] = "44444444-4444-4444-8444-444444444444";
    v["org_id"] = "55555555-5555-4555-8555-555555555555";
    v["creator_id"] = "66666666-6666-4666-8666-666666666666";
    v["title"] = "legacy row";
    v["status"] = "PASSED";
    Json::Value delta;
    delta["consensus_model"] = "MAJORITY";
    delta["quorum_percentage"] = 51;
    delta["voting_duration_sec"] = 600;
    delta["power_distribution"] = "EQUAL";
    v["config_delta"] = delta;  // дельта есть, но применена не была

    auto restored = Codec::deserializeProposal(Codec::dump(v));
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->status, ProposalStatus::PASSED);
    EXPECT_TRUE(restored->config_delta.has_value());
    EXPECT_FALSE(restored->config_delta_applied);
    EXPECT_FALSE(restored->action_applied);
}
