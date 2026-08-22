#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace voterpool {

struct Agent {
    std::string agent_id;
    std::string name;
    std::string short_description;
    std::string description;
    std::vector<std::string> tags;
    std::string api_key_hash;
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
};

}  // namespace voterpool
