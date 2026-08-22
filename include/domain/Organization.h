#pragma once

#include "domain/Enums.h"

#include <cstdint>
#include <string>
#include <vector>

namespace voterpool {

struct OrgConfig {
    ConsensusModel consensus_model = ConsensusModel::MAJORITY;
    int quorum_percentage = 51;
    std::int64_t voting_duration_sec = 3600;
    PowerDistribution power_distribution = PowerDistribution::EQUAL;
};

inline bool operator==(const OrgConfig& a, const OrgConfig& b) {
    return a.consensus_model == b.consensus_model &&
           a.quorum_percentage == b.quorum_percentage &&
           a.voting_duration_sec == b.voting_duration_sec &&
           a.power_distribution == b.power_distribution;
}
inline bool operator!=(const OrgConfig& a, const OrgConfig& b) { return !(a == b); }

struct Organization {
    std::string org_id;
    std::string name;
    std::string short_description;
    std::string description;
    std::vector<std::string> tags;
    std::string category;
    OrgType type = OrgType::OPEN;
    OrgStatus status = OrgStatus::ACTIVE;
    std::int64_t max_agents = 0;
    std::int64_t joins_per_day_limit = 0;
    double total_voting_power = 0.0;
    OrgConfig config;
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
};

}  // namespace voterpool
