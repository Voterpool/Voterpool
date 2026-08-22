#pragma once

#include <string>

namespace voterpool {

enum class ConsensusModel { MAJORITY, QUORUM_PERCENTAGE, CONSENT };
enum class PowerDistribution { EQUAL, SHARES };
enum class OrgType { OPEN, CLOSED };
enum class OrgStatus { ACTIVE, DISSOLVED };
enum class MemberRole { ADMIN, MEMBER };
enum class MemberStatus { ACTIVE, PENDING };
enum class ProposalType { STANDARD, ACTION };
enum class ProposalStatus { ACTIVE, PASSED, REJECTED, EXPIRED };
enum class VoteDecision { YES, NO, ABSTAIN };
enum class ActionKind { APPROVE_MEMBER, UPDATE_ORG_INFO };

inline const char* toString(ConsensusModel m) {
    switch (m) {
        case ConsensusModel::MAJORITY: return "MAJORITY";
        case ConsensusModel::QUORUM_PERCENTAGE: return "QUORUM_PERCENTAGE";
        case ConsensusModel::CONSENT: return "CONSENT";
    }
    return "MAJORITY";
}

inline const char* toString(PowerDistribution d) {
    return d == PowerDistribution::EQUAL ? "EQUAL" : "SHARES";
}

inline const char* toString(OrgType t) { return t == OrgType::OPEN ? "OPEN" : "CLOSED"; }
inline const char* toString(OrgStatus s) { return s == OrgStatus::ACTIVE ? "ACTIVE" : "DISSOLVED"; }
inline const char* toString(MemberRole r) { return r == MemberRole::ADMIN ? "ADMIN" : "MEMBER"; }
inline const char* toString(MemberStatus s) { return s == MemberStatus::ACTIVE ? "ACTIVE" : "PENDING"; }
inline const char* toString(ProposalType t) { return t == ProposalType::STANDARD ? "STANDARD" : "ACTION"; }
inline const char* toString(ProposalStatus s) {
    switch (s) {
        case ProposalStatus::ACTIVE: return "ACTIVE";
        case ProposalStatus::PASSED: return "PASSED";
        case ProposalStatus::REJECTED: return "REJECTED";
        case ProposalStatus::EXPIRED: return "EXPIRED";
    }
    return "ACTIVE";
}
inline const char* toString(VoteDecision d) {
    switch (d) {
        case VoteDecision::YES: return "YES";
        case VoteDecision::NO: return "NO";
        case VoteDecision::ABSTAIN: return "ABSTAIN";
    }
    return "YES";
}
inline const char* toString(ActionKind k) {
    return k == ActionKind::APPROVE_MEMBER ? "APPROVE_MEMBER" : "UPDATE_ORG_INFO";
}

}  // namespace voterpool
