#pragma once

#include "consensus/IConsensusModel.h"
#include "core/CryptoUtil.h"
#include "core/Metrics.h"
#include "mcp/tools/ToolRegistry.h"
#include "storage/Keys.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <optional>
#include <string>
#include <vector>

namespace voterpool::mcp {

inline std::size_t editDistanceSmall(const std::string& a, const std::string& b) {
    std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            std::size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

inline std::string lowered(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline std::string didYouMean(const std::string& key,
                              const std::vector<std::string>& known) {
    const std::string k = lowered(key);
    const std::string* best = nullptr;
    std::size_t bestDist = SIZE_MAX;
    for (const auto& n : known) {
        const std::string ln = lowered(n);
        if (ln == k) continue;
        // Общий значимый префикс >= 4 символов — тоже уверенная подсказка.
        std::size_t common = 0;
        while (common < k.size() && common < ln.size() && k[common] == ln[common]) ++common;
        bool prefixMatch = common >= 4 && (k.rfind(ln.substr(0, common), 0) == 0);
        auto d = editDistanceSmall(k, ln);
        if ((prefixMatch && bestDist > 2) || d < bestDist ||
            (d == bestDist && best == nullptr)) {
            best = &n;
            bestDist = prefixMatch ? 2 : d;
        }
    }
    if (!best || bestDist > 2) return "";
    return *best;
}

inline std::optional<RpcError> validateStrictArgs(const std::string& toolName,
                                                  const Json::Value& args) {
    const ToolArgMeta* meta = findArgMeta(toolName);
    if (!meta) return std::nullopt;  // у анонимных tools без аргументов схемы нет
    if (args.isNull()) return std::nullopt;  // «аргументов нет» — легитимно
    if (!args.isObject()) return RpcError::invalidParams("arguments must be an object");
    for (const auto& key : args.getMemberNames()) {
        bool found = false;
        for (const auto& [name, type] : meta->props) {
            if (name != key) continue;
            found = true;
            const Json::Value& v = args[key];
            bool typeOk = true;
            std::string expectedHuman = type;
            if (type == "string") typeOk = v.isString();
            else if (type == "integer") typeOk = v.isIntegral() && !v.isBool();
            else if (type == "number") typeOk = v.isNumeric() && !v.isBool();
            else if (type == "boolean") typeOk = v.isBool();
            else if (type == "array") typeOk = v.isArray();
            else if (type == "object") typeOk = v.isObject();
            if (!typeOk) {
                RpcError e =
                    RpcError::invalidParams("Argument must be of type " + expectedHuman + ": " + key);
                e.data["field"] = key;
                e.data["expected_type"] = expectedHuman;
                return e;
            }
            break;
        }
        if (found) continue;
        RpcError e = RpcError::invalidParams("Unknown argument: " + key);
        e.data["field"] = key;
        std::string hint = didYouMean(key, meta->names);
        if (!hint.empty()) e.data["hint"] = "did you mean \"" + hint + "\"?";
        Json::Value allowed(Json::arrayValue);
        for (const auto& n : meta->names) allowed.append(n);
        e.data["allowed_args"] = std::move(allowed);
        return e;
    }
    return std::nullopt;
}


inline Result<Organization> loadOrgForOperation(AppContext& app, const std::string& orgId) {
    auto org = app.orgs->get(orgId);
    if (!org) return Result<Organization>(RpcError::notFound("Organization", orgId));
    if (org->status == OrgStatus::DISSOLVED) return Result<Organization>(RpcError::notFound("Organization", orgId));
    return Result<Organization>(std::move(*org));
}

inline Result<Membership> requireActiveMember(AppContext& app, const std::string& orgId,
                                              const std::string& agentId) {
    auto m = app.orgs->getMembership(orgId, agentId);
    if (!m) {
        // Машинно-читаемая диагностика: агент видит, КАКАЯ личность
        // проверялась и в какую организацию (контракт подсказок).
        RpcError e = RpcError::forbidden("Agent is not a member of this organization");
        e.data["org_id"] = orgId;
        e.data["agent_id"] = agentId;
        return Result<Membership>(std::move(e));
    }
    if (m->status != MemberStatus::ACTIVE) {
        RpcError e = RpcError::forbidden("Membership is pending admin approval");
        e.data["org_id"] = orgId;
        e.data["agent_id"] = agentId;
        return Result<Membership>(std::move(e));
    }
    return Result<Membership>(std::move(*m));
}

inline Result<std::string> argString(const Json::Value& args, const std::string& name, bool required = true,
                                     bool allowEmpty = false) {
    if (!args.isMember(name)) {
        if (required) return RpcError::invalidParams("Missing required argument: " + name);
        return std::string();
    }
    if (!args[name].isString()) return RpcError::invalidParams("Argument must be a string: " + name);
    std::string v = args[name].asString();
    if (!allowEmpty && v.empty()) return RpcError::invalidParams("Argument must not be empty: " + name);
    return v;
}

inline Result<std::string> argUuid(const Json::Value& args, const std::string& name, bool required = true) {
    auto v = argString(args, name, required, false);
    if (!v.ok()) return v.error();
    if (required && !isValidUuid(v.value())) return RpcError::invalidParams("Invalid UUID format: " + name);
    return v;
}

inline Result<double> argDouble(const Json::Value& args, const std::string& name, bool required = true) {
    if (!args.isMember(name)) {
        if (required) return RpcError::invalidParams("Missing required argument: " + name);
        return 0.0;
    }
    const Json::Value& v = args[name];
    if (!v.isNumeric() || v.isBool()) return RpcError::invalidParams("Argument must be a number: " + name);
    return v.asDouble();
}

inline Result<OrgConfig> parseOrgConfig(const Json::Value& cfgJson, bool required = true,
                                        const OrgConfig* base = nullptr) {
    static const std::vector<std::string> kConfigKeys = {"consensus_model", "quorum_percentage",
                                                         "voting_duration_sec",
                                                         "power_distribution"};
    if (!cfgJson.isObject()) {
        if (required) return RpcError::invalidParams("Missing required argument: config");
        return OrgConfig{};
    }

    for (const auto& key : cfgJson.getMemberNames()) {
        if (std::find(kConfigKeys.begin(), kConfigKeys.end(), key) != kConfigKeys.end()) continue;
        RpcError e = RpcError::invalidParams("Unknown config field: " + key);
        e.data["field"] = key;
        std::string hint = didYouMean(key, kConfigKeys);
        if (!hint.empty()) e.data["hint"] = "did you mean \"" + hint + "\"?";
        Json::Value allowed(Json::arrayValue);
        for (const auto& k : kConfigKeys) allowed.append(k);
        e.data["allowed_fields"] = std::move(allowed);
        return Result<OrgConfig>(std::move(e));
    }

    Json::Value merged(Json::objectValue);
    if (base) {
        const Json::Value baseJson = Codec::orgConfigToJson(*base);
        for (const auto& key : baseJson.getMemberNames()) merged[key] = baseJson[key];
    }
    for (const auto& key : cfgJson.getMemberNames()) merged[key] = cfgJson[key];
    bool ok = false;
    OrgConfig parsed = Codec::orgConfigFromJson(merged, ok);
    if (!ok) return RpcError::invalidParams("Invalid consensus model or power distribution in config");
    if (parsed.quorum_percentage < 0 || parsed.quorum_percentage > 100)
        return RpcError::invalidParams("quorum_percentage must be in [0;100]");
    if (parsed.voting_duration_sec <= 0)
        return RpcError::businessRule("Voting duration must be greater than 0 seconds");
    if (parsed.consensus_model == ConsensusModel::CONSENT &&
        parsed.power_distribution == PowerDistribution::SHARES) {
        RpcError e = RpcError::businessRule(
            "CONSENT consensus model requires EQUAL power distribution: all votes are equal");
        e.data["consensus_model"] = toString(parsed.consensus_model);
        e.data["power_distribution"] = toString(parsed.power_distribution);
        return Result<OrgConfig>(std::move(e));
    }
    return parsed;
}

inline Json::Value orgConfigJson(const OrgConfig& c) { return Codec::orgConfigToJson(c); }

inline Json::Value allowedDecisionsJson(ConsensusModel model) {
    Json::Value arr(Json::arrayValue);
    if (auto m = makeConsensusModel(model)) {
        for (auto d : m->allowedDecisions()) arr.append(toString(d));
    }
    return arr;
}

inline void db_putOrg(AppContext& app, rocksdb::WriteBatch& batch, const Organization& org) {
    app.db->put(batch, "cf_organizations", Keys::org(org.org_id), Codec::serializeOrg(org));
}

inline void refreshOrgGauges(AppContext& app) {
    MetricsRegistry::instance().setGauge("voterpool_orgs_active", {},
                                         static_cast<std::int64_t>(app.orgs->countActive()));
}

}  // namespace voterpool::mcp
