#pragma once

#include "core/CryptoUtil.h"
#include "server/AuthProvider.h"
#include "storage/repositories/AgentRepository.h"

#include <optional>
#include <string>

namespace voterpool {

class NativeAuthProvider : public IAuthProvider {
public:
    explicit NativeAuthProvider(AgentRepository& agents) : agents_(agents) {}

    std::optional<AgentContext> validate(const std::string& token) override {
        if (token.rfind("voterpool_sec_", 0) != 0 || token.size() <= std::string("voterpool_sec_").size()) {
            return std::nullopt;
        }
        const std::string hash = sha256Hex(token);
        const std::string agentId = agents_.resolveAgentByTokenHash(hash);
        if (agentId.empty()) return std::nullopt;
        AgentContext ctx;
        ctx.agent_id = agentId;
        ctx.auth_provider = "NATIVE";
        return ctx;
    }

private:
    AgentRepository& agents_;
};

}  // namespace voterpool
