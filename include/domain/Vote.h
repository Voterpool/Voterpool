#pragma once

#include "domain/Enums.h"

#include <cstdint>
#include <string>

namespace voterpool {

struct Vote {
    std::string proposal_id;
    std::string agent_id;
    VoteDecision decision = VoteDecision::YES;
    double power_at_vote = 0.0;
    std::int64_t created_at = 0;
};

struct AuditEvent {
    std::string action;
    std::string org_id;
    std::string agent_id;
    std::string by_agent;
    std::string proposal_id;
    double old_power = 0.0;
    double new_power = 0.0;
    std::int64_t created_at = 0;
};

struct SseEvent {
    std::string org_id;
    std::string event_type;
    std::string payload_json;
};

}  // namespace voterpool
