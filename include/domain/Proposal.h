#pragma once

#include "domain/Enums.h"
#include "domain/Organization.h"

#include <cstdint>
#include <optional>
#include <string>

namespace voterpool {

struct ProposalAction {
    ActionKind kind = ActionKind::APPROVE_MEMBER;
    std::string target_agent_id;
    std::string new_name;
    std::string new_short_description;
    std::string new_description;
    bool category_set = false;
    std::string new_category;
    bool tags_set = false;
    std::vector<std::string> new_tags;
    bool max_agents_set = false;
    std::int64_t new_max_agents = 0;
    bool joins_per_day_limit_set = false;
    std::int64_t new_joins_per_day_limit = 0;
};

struct Proposal {
    std::string proposal_id;
    std::string org_id;
    std::string creator_id;
    std::string title;
    std::string description;
    ProposalType type = ProposalType::STANDARD;

    std::optional<ProposalAction> action;
    std::optional<OrgConfig> config_delta;
    OrgConfig config_at_creation;

    ProposalStatus status = ProposalStatus::ACTIVE;
    std::int64_t created_at = 0;
    std::int64_t expires_at = 0;
    std::int64_t updated_at = 0;

    double yes_power = 0.0;
    double no_power = 0.0;
    double abstain_power = 0.0;
    std::int64_t voters_count = 0;
    double total_voting_power_at_creation = 0.0;
    std::int64_t eligible_voters_at_creation = 0;
};

}  // namespace voterpool
