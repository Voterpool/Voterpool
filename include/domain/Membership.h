#pragma once

#include "domain/Enums.h"

#include <cstdint>
#include <string>

namespace voterpool {

struct Membership {
    std::string org_id;
    std::string agent_id;
    MemberRole role = MemberRole::MEMBER;
    double voting_power = 1.0;
    MemberStatus status = MemberStatus::ACTIVE;
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
};

}  // namespace voterpool
