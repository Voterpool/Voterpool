#include "consensus/ConsensusEngine.h"

#include "consensus/IConsensusModel.h"
#include "consensus/ProposalLock.h"
#include "core/IClock.h"
#include "core/Metrics.h"
#include "storage/Keys.h"
#include "storage/RocksDBWrapper.h"
#include "storage/repositories/AuditLogRepository.h"
#include "storage/repositories/IndexRepository.h"
#include "storage/repositories/OrgRepository.h"
#include "storage/repositories/ProposalRepository.h"
#include "storage/repositories/VoteRepository.h"

namespace voterpool {
namespace {

Json::Value configBrief(const OrgConfig& c) {
    Json::Value v;
    v["consensus_model"] = toString(c.consensus_model);
    v["quorum_percentage"] = c.quorum_percentage;
    return v;
}

SseEvent makeEvent(const std::string& org, const char* type, const Json::Value& payload) {
    return SseEvent{org, type, Codec::dump(payload)};
}

}  // namespace

void ConsensusEngine::emitClosed(const Proposal& p, const ClosedInfo& info) {
    Json::Value ev;
    ev["org_id"] = p.org_id;
    ev["proposal_id"] = p.proposal_id;
    ev["final_status"] = toString(p.status);
    ev["yes_power"] = p.yes_power;
    ev["no_power"] = p.no_power;
    ev["abstain_power"] = p.abstain_power;
    ev["config_delta_applied"] = info.configDeltaApplied;
    if (info.closed && info.actionKind.empty()) {
        ev["action_applied"] = Json::Value(Json::nullValue);
    } else if (!info.actionKind.empty()) {
        ev["action_applied"] = info.actionKind;
    } else {
        ev["action_applied"] = Json::Value(Json::nullValue);
    }
    d_.emit(makeEvent(p.org_id, "proposal_closed", ev));
}

void ConsensusEngine::finalizeLocked(rocksdb::WriteBatch& batch, Proposal& p, ProposalStatus finalStatus,
                                     ClosedInfo& info) {
    p.status = finalStatus;
    p.updated_at = d_.clock->nowSec();
    d_.proposals->put(batch, p);
    d_.proposals->removeActiveIndex(batch, p);

    info.closed = true;
    info.status = finalStatus;

    if (finalStatus != ProposalStatus::PASSED) {
        MetricsRegistry::instance().incCounter("voterpool_consensus_early_exit_total", {{"consensus_model", toString(p.config_at_creation.consensus_model)}});
        return;
    }

    auto orgOpt = d_.orgs->get(p.org_id);

    if (p.config_delta) {
        if (orgOpt) {
            orgOpt->config = *p.config_delta;
            orgOpt->updated_at = p.updated_at;
            d_.orgs->put(*orgOpt);
            info.configDeltaApplied = true;

            AuditEvent ae;
            ae.action = "CONFIG_CHANGED";
            ae.org_id = p.org_id;
            ae.agent_id = "";
            ae.by_agent = p.creator_id;
            ae.proposal_id = p.proposal_id;
            ae.created_at = d_.clock->nowMilli();
            d_.audit->append(batch, p.org_id, ae);
        }
    }

    if (!p.action) return;

    MetricsRegistry::instance().incCounter("voterpool_actions_applied_total", {{"kind", toString(p.action->kind)}});

    if (p.action->kind == ActionKind::APPROVE_MEMBER) {
        const std::string targetId = p.action->target_agent_id;
        auto mOpt = d_.orgs->getMembership(p.org_id, targetId);
        if (mOpt && mOpt->status == MemberStatus::PENDING && orgOpt) {
            bool limitsOk = true;
            if (orgOpt->max_agents > 0) {
                std::int64_t activeCount = d_.orgs->countActiveMembers(p.org_id);
                if (activeCount >= orgOpt->max_agents) limitsOk = false;
            }
            if (limitsOk && orgOpt->joins_per_day_limit > 0) {
                std::int64_t used = d_.indexes->getJoinLimit(p.org_id, d_.clock->nowSec());
                if (used >= orgOpt->joins_per_day_limit) limitsOk = false;
            }
            if (limitsOk) {
                mOpt->status = MemberStatus::ACTIVE;
                mOpt->updated_at = p.updated_at;
                d_.orgs->putMembership(batch, *mOpt);
                d_.indexes->removePending(batch, p.org_id, targetId);
                d_.indexes->incrementJoinLimit(batch, p.org_id, d_.clock->nowSec());
                orgOpt->total_voting_power += mOpt->voting_power;
                orgOpt->updated_at = p.updated_at;
                d_.orgs->put(*orgOpt);
                info.actionApplied = true;
                info.actionKind = "APPROVE_MEMBER";

                AuditEvent ae;
                ae.action = "MEMBER_ACTIVATED";
                ae.org_id = p.org_id;
                ae.agent_id = targetId;
                ae.by_agent = p.creator_id;
                ae.proposal_id = p.proposal_id;
                ae.old_power = 0.0;
                ae.new_power = mOpt->voting_power;
                ae.created_at = d_.clock->nowMilli();
                d_.audit->append(batch, p.org_id, ae);

                Json::Value ev;
                ev["org_id"] = p.org_id;
                ev["agent_id"] = targetId;
                ev["role"] = toString(mOpt->role);
                ev["voting_power"] = mOpt->voting_power;
                ev["new_total_voting_power"] = orgOpt->total_voting_power;
                d_.emit(makeEvent(p.org_id, "member_joined", ev));
            }
        }
        return;
    }

    if (p.action->kind == ActionKind::UPDATE_ORG_INFO && orgOpt) {
        Organization updated = *orgOpt;
        const ProposalAction& act = *p.action;
        Organization old = *orgOpt;
        if (!act.new_name.empty()) updated.name = act.new_name;
        if (!act.new_short_description.empty()) updated.short_description = act.new_short_description;
        if (!act.new_description.empty()) updated.description = act.new_description;
        if (act.category_set) updated.category = act.new_category;
        if (act.tags_set) updated.tags = act.new_tags;
        if (act.max_agents_set) updated.max_agents = act.new_max_agents;
        if (act.joins_per_day_limit_set) updated.joins_per_day_limit = act.new_joins_per_day_limit;
        updated.updated_at = p.updated_at;

        d_.orgs->put(updated);
        d_.indexes->removeName(batch, old);
        d_.indexes->setName(batch, updated);
        d_.indexes->removeTags(batch, old);
        d_.indexes->setTags(batch, updated);
        d_.indexes->removeCategory(batch, old);
        d_.indexes->setCategory(batch, updated);
        info.actionApplied = true;
        info.actionKind = "UPDATE_ORG_INFO";

        AuditEvent ae;
        ae.action = "ORG_INFO_UPDATED";
        ae.org_id = p.org_id;
        ae.agent_id = "";
        ae.by_agent = p.creator_id;
        ae.proposal_id = p.proposal_id;
        ae.created_at = d_.clock->nowMilli();
        d_.audit->append(batch, p.org_id, ae);
    }
}

Result<VoteReceipt> ConsensusEngine::castVote(const std::string& agentId, const std::string& proposalId,
                                              VoteDecision decision) {
    const std::string orgId = d_.proposals->lookupOrg(proposalId);
    if (orgId.empty()) return RpcError::notFound("Proposal", proposalId);

    auto membership = d_.orgs->getMembership(orgId, agentId);
    if (!membership || membership->status != MemberStatus::ACTIVE) {
        return RpcError::forbidden("Agent is not an active member");
    }

    auto lock = d_.locks->acquire(proposalId);

    auto pOpt = d_.proposals->get(orgId, proposalId);
    if (!pOpt) return RpcError::notFound("Proposal", proposalId);
    Proposal& p = *pOpt;

    if (p.status != ProposalStatus::ACTIVE) {
        RpcError e = RpcError::conflict("Proposal is already closed");
        e.data["current_status"] = toString(p.status);
        return e;
    }
    const std::int64_t now = d_.clock->nowSec();
    if (now >= p.expires_at) {
        RpcError e = RpcError::conflict("Voting is closed");
        e.data["current_status"] = toString(p.status);
        return e;
    }

    auto model = makeConsensusModel(p.config_at_creation.consensus_model);
    if (!model || model->allowedDecisions().count(decision) == 0) {
        RpcError e = RpcError::businessRule("Decision is not allowed by the consensus model");
        e.data["consensus_model"] = toString(p.config_at_creation.consensus_model);
        Json::Value allowed(Json::arrayValue);
        for (auto d : model->allowedDecisions()) allowed.append(toString(d));
        e.data["allowed"] = allowed;
        return e;
    }

    auto existing = d_.votes->get(orgId, proposalId, agentId);
    if (existing) {
        RpcError e = RpcError::conflict("Agent has already voted on this proposal");
        e.data["proposal_id"] = proposalId;
        e.data["previous_decision"] = toString(existing->decision);
        return e;
    }

    const double powerAtVote = membership->voting_power;
    Vote v;
    v.proposal_id = proposalId;
    v.agent_id = agentId;
    v.decision = decision;
    v.power_at_vote = powerAtVote;
    v.created_at = now;

    rocksdb::WriteBatch batch;
    d_.votes->put(batch, orgId, v);
    switch (decision) {
        case VoteDecision::YES: p.yes_power += powerAtVote; break;
        case VoteDecision::NO: p.no_power += powerAtVote; break;
        case VoteDecision::ABSTAIN: p.abstain_power += powerAtVote; break;
    }
    p.voters_count += 1;
    p.updated_at = now;

    ClosedInfo closed;
    auto eval = model->evaluate(p, false);
    if (eval.finalStatus.has_value()) {
        finalizeLocked(batch, p, *eval.finalStatus, closed);
    } else {
        d_.proposals->put(batch, p);
    }

    if (!d_.db->commit(batch)) {
        return RpcError::internal("Storage write failed");
    }

    MetricsRegistry::instance().incCounter("voterpool_votes_cast_total", {{"decision", toString(decision)}});
    MetricsRegistry::instance().setGauge("voterpool_proposals_active", {}, d_.proposals->countActiveGauge());

    {
        Json::Value ev;
        ev["org_id"] = orgId;
        ev["proposal_id"] = proposalId;
        ev["agent_id"] = agentId;
        ev["decision"] = toString(decision);
        ev["current_yes_power"] = p.yes_power;
        ev["current_no_power"] = p.no_power;
        ev["current_abstain_power"] = p.abstain_power;
        ev["total_voting_power_at_creation"] = p.total_voting_power_at_creation;
        ev["voters_count"] = static_cast<Json::Int64>(p.voters_count);
        ev["proposal_status"] = toString(p.status);
        d_.emit(makeEvent(orgId, "vote_cast", ev));
    }
    if (closed.closed) {
        emitClosed(p, closed);
        d_.locks->forget(proposalId);
    }

    VoteReceipt r;
    r.proposal_id = proposalId;
    r.decision = toString(decision);
    r.power_applied = powerAtVote;
    r.proposal_status = p.status;
    r.current_yes_power = p.yes_power;
    r.current_no_power = p.no_power;
    return r;
}

bool ConsensusEngine::closeProposalByTimer(const std::string& orgId, const std::string& proposalId) {
    auto lock = d_.locks->acquire(proposalId);
    auto pOpt = d_.proposals->get(orgId, proposalId);
    rocksdb::WriteBatch batch;
    if (!pOpt) {
        std::string prefix = Keys::activeProposalPrefix();
        std::vector<std::string> stale;
        {
            auto it = d_.db->newIterator("cf_indexes");
            for (it->Seek(prefix + std::string(19, '0') + ":" + proposalId); it->Valid(); it->Next()) {
                std::string k = it->key().ToString();
                if (k.rfind(prefix, 0) != 0 || k.size() <= prefix.size() + 20 ||
                    k.substr(prefix.size() + 20) != proposalId)
                    break;
                stale.push_back(k);
            }
        }
        for (const auto& k : stale) d_.db->remove(batch, "cf_indexes", k);
        bool ok = d_.db->commit(batch);
        d_.locks->forget(proposalId);
        return ok;
    }
    Proposal& p = *pOpt;
    ClosedInfo info;
    if (p.status != ProposalStatus::ACTIVE) {
        d_.proposals->removeActiveIndex(batch, p);
    } else {
        auto model = makeConsensusModel(p.config_at_creation.consensus_model);
        auto eval = model ? model->evaluate(p, true) : Evaluation{ProposalStatus::EXPIRED};
        finalizeLocked(batch, p, eval.finalStatus.value_or(ProposalStatus::EXPIRED), info);
    }
    if (!d_.db->commit(batch)) {
        return false;
    }
    d_.locks->forget(proposalId);

    if (info.closed) {
        MetricsRegistry::instance().incCounter("voterpool_proposals_closed_total", {{"final_status", toString(info.status)}});
        MetricsRegistry::instance().setGauge("voterpool_proposals_active", {}, d_.proposals->countActiveGauge());
        emitClosed(p, info);
    }
    return true;
}

void ConsensusEngine::closeExpired(std::int64_t nowSec, size_t maxBatch) {
    struct Item {
        std::string proposalId;
        std::string orgId;
    };
    std::vector<Item> expired;
    {
        auto it = d_.db->newIterator("cf_indexes");
        const std::string prefix = Keys::activeProposalPrefix();
        for (it->Seek(prefix); it->Valid() && expired.size() < maxBatch; it->Next()) {
            std::string k = it->key().ToString();
            if (k.rfind(prefix, 0) != 0) break;
            std::int64_t exp = 0;
            try {
                exp = std::stoll(k.substr(prefix.size(), 19));
            } catch (...) {
                continue;
            }
            if (exp > nowSec) break;
            expired.push_back({k.substr(prefix.size() + 20), it->value().ToString()});
        }
    }
    for (const auto& item : expired) {
        closeProposalByTimer(item.orgId, item.proposalId);
    }
}

std::vector<SseEvent> ConsensusEngine::expireAllForDissolve(rocksdb::WriteBatch& batch, const std::string& orgId) {
    std::vector<SseEvent> events;
    for (const auto& p : d_.proposals->listByOrg(orgId)) {
        if (p.status != ProposalStatus::ACTIVE) continue;
        auto lock = d_.locks->acquire(p.proposal_id);
        auto fresh = d_.proposals->get(orgId, p.proposal_id);
        if (!fresh || fresh->status != ProposalStatus::ACTIVE) continue;
        Proposal& pr = *fresh;
        ClosedInfo info;
        info.closed = true;
        info.status = ProposalStatus::EXPIRED;
        pr.status = ProposalStatus::EXPIRED;
        pr.updated_at = d_.clock->nowSec();
        d_.proposals->put(batch, pr);
        d_.proposals->removeActiveIndex(batch, pr);
        d_.locks->forget(pr.proposal_id);
        MetricsRegistry::instance().incCounter("voterpool_proposals_closed_total", {{"final_status", "EXPIRED"}});
        Json::Value ev;
        ev["org_id"] = orgId;
        ev["proposal_id"] = pr.proposal_id;
        ev["final_status"] = "EXPIRED";
        ev["yes_power"] = pr.yes_power;
        ev["no_power"] = pr.no_power;
        ev["abstain_power"] = pr.abstain_power;
        ev["config_delta_applied"] = false;
        ev["action_applied"] = Json::Value(Json::nullValue);
        events.push_back(makeEvent(orgId, "proposal_closed", ev));
    }
    MetricsRegistry::instance().setGauge("voterpool_proposals_active", {}, d_.proposals->countActiveGauge());
    return events;
}

}  // namespace voterpool
