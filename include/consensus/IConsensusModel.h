#pragma once

#include "domain/Enums.h"
#include "domain/Proposal.h"

#include <memory>
#include <optional>
#include <set>
#include <string>

namespace voterpool {

struct Evaluation {
    std::optional<ProposalStatus> finalStatus;
};

class IConsensusModel {
public:
    virtual ~IConsensusModel() = default;
    virtual const std::set<VoteDecision>& allowedDecisions() const = 0;
    virtual Evaluation evaluate(const Proposal& p, bool timeExpired) const = 0;
    virtual const char* name() const = 0;
};

constexpr double kEps = 1e-9;

class MajorityModel : public IConsensusModel {
public:
    const std::set<VoteDecision>& allowedDecisions() const override {
        static const std::set<VoteDecision> s{VoteDecision::YES, VoteDecision::NO};
        return s;
    }
    const char* name() const override { return "MAJORITY"; }
    Evaluation evaluate(const Proposal& p, bool timeExpired) const override {
        const double T = p.total_voting_power_at_creation;
        const double V = p.yes_power + p.no_power + p.abstain_power;
        if (p.yes_power > T / 2.0 + kEps) return {ProposalStatus::PASSED};
        if (timeExpired) return {ProposalStatus::REJECTED};
        if (p.no_power >= T / 2.0 - kEps) return {ProposalStatus::REJECTED};
        const double maxY = p.yes_power + (T - V);
        if (maxY <= T / 2.0 + kEps) return {ProposalStatus::REJECTED};
        return {std::nullopt};
    }
};

class QuorumModel : public IConsensusModel {
public:
    const std::set<VoteDecision>& allowedDecisions() const override {
        static const std::set<VoteDecision> s{VoteDecision::YES, VoteDecision::NO};
        return s;
    }
    const char* name() const override { return "QUORUM_PERCENTAGE"; }
    Evaluation evaluate(const Proposal& p, bool timeExpired) const override {
        const double T = p.total_voting_power_at_creation;
        const double V = p.yes_power + p.no_power;
        const double qreq = T * static_cast<double>(p.config_at_creation.quorum_percentage) / 100.0;
        const bool quorumReached = V >= qreq - kEps;
        if (quorumReached) {
            if (p.yes_power > p.no_power + kEps) return {ProposalStatus::PASSED};
            return {ProposalStatus::REJECTED};
        }
        if (timeExpired) return {ProposalStatus::EXPIRED};
        if (T < qreq - kEps) return {ProposalStatus::REJECTED};
        const double maxY = p.yes_power + (T - V);
        if (maxY <= p.no_power + kEps) return {ProposalStatus::REJECTED};
        return {std::nullopt};
    }
};

class ConsentModel : public IConsensusModel {
public:
    const std::set<VoteDecision>& allowedDecisions() const override {
        static const std::set<VoteDecision> s{VoteDecision::YES, VoteDecision::NO, VoteDecision::ABSTAIN};
        return s;
    }
    const char* name() const override { return "CONSENT"; }
    Evaluation evaluate(const Proposal& p, bool timeExpired) const override {
        if (p.no_power > kEps) return {ProposalStatus::REJECTED};
        if (p.yes_power > kEps && p.voters_count > 0) return {ProposalStatus::PASSED};
        if (timeExpired) return {ProposalStatus::EXPIRED};
        return {std::nullopt};
    }
};

std::unique_ptr<IConsensusModel> makeConsensusModel(ConsensusModel model);

}  // namespace voterpool
