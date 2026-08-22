#pragma once

#include "tests/common/Harness.h"

namespace voterpool::testing {

inline Json::Value orgConfigArgs(const char* model, int durationSec, const char* dist = "EQUAL", int quorum = 51) {
    Json::Value cfg;
    cfg["consensus_model"] = model;
    cfg["voting_duration_sec"] = durationSec;
    cfg["power_distribution"] = dist;
    cfg["quorum_percentage"] = quorum;
    return cfg;
}

inline Json::Value createOrg(Harness& h, const AgentContext& creator, const std::string& name,
                             const char* type, Json::Value cfg) {
    Json::Value args;
    args["name"] = name;
    args["type"] = type;
    if (!cfg.isNull()) args["config"] = cfg;
    return h.call("create_organization", args, &creator);
}

inline Json::Value createProposal(Harness& h, const AgentContext& author, const std::string& orgId,
                                  const std::string& title) {
    Json::Value args;
    args["org_id"] = orgId;
    args["title"] = title;
    return h.call("create_proposal", args, &author);
}

inline Json::Value vote(Harness& h, const AgentContext& voter, const std::string& proposalId,
                        const char* decision) {
    Json::Value args;
    args["proposal_id"] = proposalId;
    args["decision"] = decision;
    return h.call("cast_vote", args, &voter);
}

inline Json::Value getOrg(Harness& h, const std::string& orgId, const AgentContext& viewer) {
    Json::Value args;
    args["org_id"] = orgId;
    return h.call("get_organization", args, &viewer);
}

}  // namespace voterpool::testing
