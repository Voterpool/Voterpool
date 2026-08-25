#pragma once

#include "core/CryptoUtil.h"
#include "core/Metrics.h"
#include "mcp/tools/ToolRegistry.h"
#include "storage/Keys.h"

namespace voterpool::mcp {

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
        RpcError e = RpcError::forbidden("Agent is not a member of this organization");
        e.data["org_id"] = orgId;
        return Result<Membership>(std::move(e));
    }
    if (m->status != MemberStatus::ACTIVE) {
        RpcError e = RpcError::forbidden("Membership is pending admin approval");
        e.data["org_id"] = orgId;
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
    if (!cfgJson.isObject()) {
        if (required) return RpcError::invalidParams("Missing required argument: config");
        return OrgConfig{};
    }
    // Дельта мержится поверх действующей конфигурации (design D3):
    // отсутствующие поля наследуются от базы, а не от значений по умолчанию;
    // валидации прогоняются по смерженному итогу.
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

inline void db_putOrg(AppContext& app, rocksdb::WriteBatch& batch, const Organization& org) {
    app.db->put(batch, "cf_organizations", Keys::org(org.org_id), Codec::serializeOrg(org));
}

inline void refreshOrgGauges(AppContext& app) {
    MetricsRegistry::instance().setGauge("voterpool_orgs_active", {},
                                         static_cast<std::int64_t>(app.orgs->countActive()));
}

}  // namespace voterpool::mcp
