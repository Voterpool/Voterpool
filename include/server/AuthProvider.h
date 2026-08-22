#pragma once

#include <optional>
#include <string>

namespace voterpool {

struct AgentContext {
    std::string agent_id;
    std::string auth_provider;
    bool is_admin = false;
};

class IAuthProvider {
public:
    virtual ~IAuthProvider() = default;
    virtual std::optional<AgentContext> validate(const std::string& token) = 0;
};

}  // namespace voterpool
