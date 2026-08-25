#pragma once

#include "domain/Entities.h"

#include <json/json.h>

#include <cstdint>
#include <string>
#include <vector>

namespace voterpool::Codec {

inline Json::Value tagsToJson(const std::vector<std::string>& tags) {
    Json::Value arr(Json::arrayValue);
    for (const auto& t : tags) arr.append(t);
    return arr;
}

inline std::vector<std::string> tagsFromJson(const Json::Value& v) {
    std::vector<std::string> out;
    if (v.isArray()) {
        for (const auto& t : v) out.push_back(t.asString());
    }
    return out;
}

inline std::string dump(const Json::Value& v) {
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    return Json::writeString(b, v);
}

inline std::optional<Json::Value> parse(const std::string& s) {
    Json::Value root;
    Json::CharReaderBuilder b;
    std::string errs;
    std::istringstream iss(s);
    if (!Json::parseFromStream(b, iss, &root, &errs)) return std::nullopt;
    return root;
}

inline Json::Value orgConfigToJson(const OrgConfig& c) {
    Json::Value v;
    v["consensus_model"] = toString(c.consensus_model);
    v["quorum_percentage"] = c.quorum_percentage;
    v["voting_duration_sec"] = static_cast<Json::Int64>(c.voting_duration_sec);
    v["power_distribution"] = toString(c.power_distribution);
    return v;
}

inline OrgConfig orgConfigFromJson(const Json::Value& v, bool& ok) {
    OrgConfig c;
    ok = true;
    const std::string model = v.get("consensus_model", "MAJORITY").asString();
    if (model == "MAJORITY") c.consensus_model = ConsensusModel::MAJORITY;
    else if (model == "QUORUM_PERCENTAGE") c.consensus_model = ConsensusModel::QUORUM_PERCENTAGE;
    else if (model == "CONSENT") c.consensus_model = ConsensusModel::CONSENT;
    else ok = false;
    c.quorum_percentage = v.get("quorum_percentage", 51).asInt();
    c.voting_duration_sec = v.get("voting_duration_sec", 3600).asInt64();
    const std::string dist = v.get("power_distribution", "EQUAL").asString();
    if (dist == "EQUAL") c.power_distribution = PowerDistribution::EQUAL;
    else if (dist == "SHARES") c.power_distribution = PowerDistribution::SHARES;
    else ok = false;
    return c;
}

inline std::string serializeAgent(const Agent& a) {
    Json::Value v;
    v["agent_id"] = a.agent_id;
    v["name"] = a.name;
    v["short_description"] = a.short_description;
    v["description"] = a.description;
    v["tags"] = tagsToJson(a.tags);
    v["api_key_hash"] = a.api_key_hash;
    v["created_at"] = static_cast<Json::Int64>(a.created_at);
    v["updated_at"] = static_cast<Json::Int64>(a.updated_at);
    return dump(v);
}

inline std::optional<Agent> deserializeAgent(const std::string& s) {
    auto root = parse(s);
    if (!root) return std::nullopt;
    Agent a;
    a.agent_id = root->get("agent_id", "").asString();
    a.name = root->get("name", "").asString();
    a.short_description = root->get("short_description", "").asString();
    a.description = root->get("description", "").asString();
    a.tags = tagsFromJson((*root)["tags"]);
    a.api_key_hash = root->get("api_key_hash", "").asString();
    a.created_at = root->get("created_at", 0).asInt64();
    a.updated_at = root->get("updated_at", 0).asInt64();
    return a;
}

inline std::string serializeOrg(const Organization& o) {
    Json::Value v;
    v["org_id"] = o.org_id;
    v["name"] = o.name;
    v["short_description"] = o.short_description;
    v["description"] = o.description;
    v["tags"] = tagsToJson(o.tags);
    v["category"] = o.category;
    v["type"] = toString(o.type);
    v["status"] = toString(o.status);
    v["max_agents"] = static_cast<Json::Int64>(o.max_agents);
    v["joins_per_day_limit"] = static_cast<Json::Int64>(o.joins_per_day_limit);
    v["total_voting_power"] = o.total_voting_power;
    v["config"] = orgConfigToJson(o.config);
    v["created_at"] = static_cast<Json::Int64>(o.created_at);
    v["updated_at"] = static_cast<Json::Int64>(o.updated_at);
    return dump(v);
}

inline std::optional<Organization> deserializeOrg(const std::string& s) {
    auto root = parse(s);
    if (!root) return std::nullopt;
    Organization o;
    o.org_id = root->get("org_id", "").asString();
    o.name = root->get("name", "").asString();
    o.short_description = root->get("short_description", "").asString();
    o.description = root->get("description", "").asString();
    o.tags = tagsFromJson((*root)["tags"]);
    o.category = root->get("category", "").asString();
    const std::string type = root->get("type", "OPEN").asString();
    o.type = (type == "CLOSED") ? OrgType::CLOSED : OrgType::OPEN;
    o.status = (root->get("status", "ACTIVE").asString() == "DISSOLVED") ? OrgStatus::DISSOLVED : OrgStatus::ACTIVE;
    o.max_agents = root->get("max_agents", 0).asInt64();
    o.joins_per_day_limit = root->get("joins_per_day_limit", 0).asInt64();
    o.total_voting_power = root->get("total_voting_power", 0.0).asDouble();
    bool ok = false;
    o.config = orgConfigFromJson((*root)["config"], ok);
    o.created_at = root->get("created_at", 0).asInt64();
    o.updated_at = root->get("updated_at", 0).asInt64();
    return o;
}

inline std::string serializeMembership(const Membership& m) {
    Json::Value v;
    v["org_id"] = m.org_id;
    v["agent_id"] = m.agent_id;
    v["role"] = toString(m.role);
    v["voting_power"] = m.voting_power;
    v["status"] = toString(m.status);
    v["created_at"] = static_cast<Json::Int64>(m.created_at);
    v["updated_at"] = static_cast<Json::Int64>(m.updated_at);
    return dump(v);
}

inline std::optional<Membership> deserializeMembership(const std::string& s) {
    auto root = parse(s);
    if (!root) return std::nullopt;
    Membership m;
    m.org_id = root->get("org_id", "").asString();
    m.agent_id = root->get("agent_id", "").asString();
    m.role = (root->get("role", "MEMBER").asString() == "ADMIN") ? MemberRole::ADMIN : MemberRole::MEMBER;
    m.voting_power = root->get("voting_power", 1.0).asDouble();
    m.status = (root->get("status", "ACTIVE").asString() == "PENDING") ? MemberStatus::PENDING : MemberStatus::ACTIVE;
    m.created_at = root->get("created_at", 0).asInt64();
    m.updated_at = root->get("updated_at", 0).asInt64();
    return m;
}

inline Json::Value actionToJson(const ProposalAction& act) {
    Json::Value v;
    v["kind"] = toString(act.kind);
    Json::Value payload;
    if (act.kind == ActionKind::APPROVE_MEMBER) {
        payload["target_agent_id"] = act.target_agent_id;
    } else {
        if (!act.new_name.empty()) payload["name"] = act.new_name;
        if (!act.new_short_description.empty()) payload["short_description"] = act.new_short_description;
        if (!act.new_description.empty()) payload["description"] = act.new_description;
        if (act.category_set) payload["category"] = act.new_category;
        if (act.tags_set) payload["tags"] = tagsToJson(act.new_tags);
        if (act.max_agents_set) payload["max_agents"] = static_cast<Json::Int64>(act.new_max_agents);
        if (act.joins_per_day_limit_set) payload["joins_per_day_limit"] = static_cast<Json::Int64>(act.new_joins_per_day_limit);
    }
    v["payload"] = payload;
    return v;
}

inline ProposalAction actionFromJson(const Json::Value& v, bool& ok) {
    ProposalAction act;
    ok = false;
    const std::string kind = v.get("kind", "").asString();
    const Json::Value& payload = v.get("payload", Json::Value(Json::objectValue));
    if (kind == "APPROVE_MEMBER") {
        act.kind = ActionKind::APPROVE_MEMBER;
        act.target_agent_id = payload.get("target_agent_id", "").asString();
        ok = !act.target_agent_id.empty();
    } else if (kind == "UPDATE_ORG_INFO") {
        act.kind = ActionKind::UPDATE_ORG_INFO;
        ok = !payload.isNull() && payload.isObject() && !payload.empty();
        if (ok) {
            if (payload.isMember("name")) act.new_name = payload["name"].asString();
            if (payload.isMember("short_description")) act.new_short_description = payload["short_description"].asString();
            if (payload.isMember("description")) act.new_description = payload["description"].asString();
            if (payload.isMember("category")) { act.category_set = true; act.new_category = payload["category"].asString(); }
            if (payload.isMember("tags")) { act.tags_set = true; act.new_tags = tagsFromJson(payload["tags"]); }
            if (payload.isMember("max_agents")) { act.max_agents_set = true; act.new_max_agents = payload["max_agents"].asInt64(); }
            if (payload.isMember("joins_per_day_limit")) { act.joins_per_day_limit_set = true; act.new_joins_per_day_limit = payload["joins_per_day_limit"].asInt64(); }
        }
    }
    return act;
}

inline std::string serializeProposal(const Proposal& p) {
    Json::Value v;
    v["proposal_id"] = p.proposal_id;
    v["org_id"] = p.org_id;
    v["creator_id"] = p.creator_id;
    v["title"] = p.title;
    v["description"] = p.description;
    v["type"] = toString(p.type);
    if (p.action) v["action"] = actionToJson(*p.action);
    else v["action"] = Json::Value(Json::nullValue);
    v["status"] = toString(p.status);
    v["config_delta_applied"] = p.config_delta_applied;
    v["action_applied"] = p.action_applied;
    v["created_at"] = static_cast<Json::Int64>(p.created_at);
    v["expires_at"] = static_cast<Json::Int64>(p.expires_at);
    v["updated_at"] = static_cast<Json::Int64>(p.updated_at);
    v["yes_power"] = p.yes_power;
    v["no_power"] = p.no_power;
    v["abstain_power"] = p.abstain_power;
    v["voters_count"] = static_cast<Json::Int64>(p.voters_count);
    v["total_voting_power_at_creation"] = p.total_voting_power_at_creation;
    v["eligible_voters_at_creation"] = static_cast<Json::Int64>(p.eligible_voters_at_creation);
    v["config_delta"] = p.config_delta ? orgConfigToJson(*p.config_delta) : Json::Value(Json::nullValue);
    v["config_at_creation"] = orgConfigToJson(p.config_at_creation);
    return dump(v);
}

inline std::optional<Proposal> deserializeProposal(const std::string& s) {
    auto root = parse(s);
    if (!root) return std::nullopt;
    Proposal p;
    p.proposal_id = root->get("proposal_id", "").asString();
    p.org_id = root->get("org_id", "").asString();
    p.creator_id = root->get("creator_id", "").asString();
    p.title = root->get("title", "").asString();
    p.description = root->get("description", "").asString();
    p.type = (root->get("type", "STANDARD").asString() == "ACTION") ? ProposalType::ACTION : ProposalType::STANDARD;
    const Json::Value& action = (*root)["action"];
    if (!action.isNull() && action.isObject()) {
        bool ok = false;
        auto act = actionFromJson(action, ok);
        if (ok) p.action = act;
    }
    p.status = ProposalStatus::ACTIVE;
    const std::string st = root->get("status", "ACTIVE").asString();
    if (st == "PASSED") p.status = ProposalStatus::PASSED;
    else if (st == "REJECTED") p.status = ProposalStatus::REJECTED;
    else if (st == "EXPIRED") p.status = ProposalStatus::EXPIRED;
    p.created_at = root->get("created_at", 0).asInt64();
    p.config_delta_applied = root->get("config_delta_applied", false).asBool();
    p.action_applied = root->get("action_applied", false).asBool();
    p.expires_at = root->get("expires_at", 0).asInt64();
    p.updated_at = root->get("updated_at", 0).asInt64();
    p.yes_power = root->get("yes_power", 0.0).asDouble();
    p.no_power = root->get("no_power", 0.0).asDouble();
    p.abstain_power = root->get("abstain_power", 0.0).asDouble();
    p.voters_count = root->get("voters_count", 0).asInt64();
    p.total_voting_power_at_creation = root->get("total_voting_power_at_creation", 0.0).asDouble();
    p.eligible_voters_at_creation = root->get("eligible_voters_at_creation", 0).asInt64();
    const Json::Value& delta = (*root)["config_delta"];
    if (!delta.isNull() && delta.isObject()) {
        bool ok = false;
        auto cfg = orgConfigFromJson(delta, ok);
        if (ok) p.config_delta = cfg;
    }
    const Json::Value& cac = (*root)["config_at_creation"];
    bool ok2 = false;
    p.config_at_creation = cac.isObject() ? orgConfigFromJson(cac, ok2) : OrgConfig{};
    return p;
}

inline std::string serializeVote(const Vote& v) {
    Json::Value j;
    j["proposal_id"] = v.proposal_id;
    j["agent_id"] = v.agent_id;
    j["decision"] = toString(v.decision);
    j["power_at_vote"] = v.power_at_vote;
    j["voted_at"] = static_cast<Json::Int64>(v.created_at);
    j["created_at"] = static_cast<Json::Int64>(v.created_at);
    j["updated_at"] = static_cast<Json::Int64>(v.created_at);
    return dump(j);
}

inline std::optional<Vote> deserializeVote(const std::string& s) {
    auto root = parse(s);
    if (!root) return std::nullopt;
    Vote v;
    v.proposal_id = root->get("proposal_id", "").asString();
    v.agent_id = root->get("agent_id", "").asString();
    const std::string d = root->get("decision", "YES").asString();
    if (d == "NO") v.decision = VoteDecision::NO;
    else if (d == "ABSTAIN") v.decision = VoteDecision::ABSTAIN;
    else v.decision = VoteDecision::YES;
    v.power_at_vote = root->get("power_at_vote", 0.0).asDouble();
    v.created_at = root->get("created_at", 0).asInt64();
    return v;
}

inline std::string serializeAudit(const AuditEvent& e) {
    Json::Value v;
    v["action"] = e.action;
    v["org_id"] = e.org_id;
    v["agent_id"] = e.agent_id;
    v["by_agent"] = e.by_agent;
    v["proposal_id"] = e.proposal_id.empty() ? Json::Value(Json::nullValue) : Json::Value(e.proposal_id);
    Json::Value changes;
    changes["old_power"] = e.old_power;
    changes["new_power"] = e.new_power;
    v["changes"] = changes;
    v["created_at"] = static_cast<Json::Int64>(e.created_at);
    return dump(v);
}

inline std::optional<AuditEvent> deserializeAudit(const std::string& s) {
    auto root = parse(s);
    if (!root) return std::nullopt;
    AuditEvent e;
    e.action = root->get("action", "").asString();
    e.org_id = root->get("org_id", "").asString();
    e.agent_id = root->get("agent_id", "").asString();
    e.by_agent = root->get("by_agent", "").asString();
    const Json::Value pid = (*root)["proposal_id"];
    e.proposal_id = pid.isNull() ? "" : pid.asString();
    const Json::Value& ch = (*root)["changes"];
    e.old_power = ch.get("old_power", 0.0).asDouble();
    e.new_power = ch.get("new_power", 0.0).asDouble();
    e.created_at = root->get("created_at", 0).asInt64();
    return e;
}

}  // namespace voterpool::Codec
