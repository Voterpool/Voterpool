#include "consensus/ConsensusEngine.h"
#include "tests/common/Scenario.h"

#include <gtest/gtest.h>

using namespace voterpool;
using namespace voterpool::testing;

namespace {

// Сумма всех семплов семейства счётчика из expose() (Prometheus text format).
std::int64_t counterTotal(const std::string& family) {
    const std::string text = MetricsRegistry::instance().expose();
    std::int64_t total = 0;
    size_t pos = 0;
    while ((pos = text.find(family, pos)) != std::string::npos) {
        size_t lineEnd = text.find('\n', pos);
        if (lineEnd == std::string::npos) lineEnd = text.size();
        std::string line = text.substr(pos, lineEnd - pos);
        char next = line.size() > family.size() ? line[family.size()] : '\0';
        if (next == '{' || next == ' ') {
            try {
                total += std::stoll(line.substr(line.rfind(' ') + 1));
            } catch (...) {
            }
        }
        pos = lineEnd;
    }
    return total;
}

Json::Value closedPayload(const std::vector<SseEvent>& events) {
    for (const auto& e : events) {
        if (e.event_type == "proposal_closed") {
            auto parsed = Codec::parse(e.payload_json);
            if (parsed) return *parsed;
        }
    }
    return Json::Value(Json::nullValue);
}

struct ClosedOrg {
    std::shared_ptr<Harness> h;
    AgentContext creator;
    AgentContext candidate;
    std::string orgId;
};

ClosedOrg makeClosedOrg(const std::string& name, const char* model = "CONSENT") {
    ClosedOrg c;
    c.h = Harness::create();
    c.creator = c.h->registerAgent(name + "-admin");
    Json::Value args;
    args["name"] = name + " Org";
    args["type"] = "CLOSED";
    args["config"] = orgConfigArgs(model, 600);
    Json::Value out = c.h->call("create_organization", args, &c.creator);
    c.orgId = out["org_id"].asString();
    c.candidate = c.h->registerAgent(name + "-candidate");
    Json::Value joinArgs;
    joinArgs["org_id"] = c.orgId;
    auto joined = c.h->call("join_organization", joinArgs, &c.candidate);
    EXPECT_EQ(joined["status"].asString(), "PENDING");
    return c;
}

Json::Value approveProposal(ClosedOrg& c) {
    Json::Value args;
    args["org_id"] = c.orgId;
    args["title"] = "Approve " + c.candidate.agent_id;
    Json::Value action;
    action["kind"] = "APPROVE_MEMBER";
    Json::Value payload;
    payload["target_agent_id"] = c.candidate.agent_id;
    action["payload"] = payload;
    args["action"] = action;
    return c.h->call("create_proposal", args, &c.creator);
}

}  // namespace

TEST(Actions, ApproveMemberActivatesPendingViaConsensus) {
    ClosedOrg c = makeClosedOrg("Approve");
    Json::Value pOut = approveProposal(c);
    ASSERT_FALSE(c.h->isError(pOut));
    std::string pid = pOut["proposal_id"].asString();

    EXPECT_EQ(c.h->errorCode(vote(*c.h, c.creator, pid, "YES")), 0);

    auto m = c.h->app->orgs->getMembership(c.orgId, c.candidate.agent_id);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->status, MemberStatus::ACTIVE);
    EXPECT_FALSE(c.h->app->indexes->hasPending(c.orgId, c.candidate.agent_id));
    EXPECT_EQ(c.h->app->indexes->getJoinLimit(c.orgId, 1700000000), 1);

    auto org = c.h->app->orgs->get(c.orgId);
    EXPECT_DOUBLE_EQ(org->total_voting_power, 2.0);

    bool foundInReverseIndex = false;
    for (const auto& om : c.h->app->orgs->listOrgsOfAgent(c.candidate.agent_id)) {
        if (om.org_id == c.orgId && om.status == MemberStatus::ACTIVE) foundInReverseIndex = true;
    }
    EXPECT_TRUE(foundInReverseIndex);
}

TEST(Actions, ApproveMemberIsIdempotentOnReactivation) {
    ClosedOrg c = makeClosedOrg("Idem");
    Json::Value first = approveProposal(c);
    std::string pid1 = first["proposal_id"].asString();
    vote(*c.h, c.creator, pid1, "YES");

    Json::Value second = approveProposal(c);
    std::string pid2 = second["proposal_id"].asString();
    vote(*c.h, c.creator, pid2, "YES");

    auto org = c.h->app->orgs->get(c.orgId);
    EXPECT_DOUBLE_EQ(org->total_voting_power, 2.0);
    EXPECT_EQ(c.h->app->indexes->getJoinLimit(c.orgId, 1700000000), 1);
}

TEST(Actions, ApproveMemberSkippedWhenOrgFull) {
    ClosedOrg c = makeClosedOrg("Full");
    auto orgBefore = c.h->app->orgs->get(c.orgId);
    Organization limited = *orgBefore;
    limited.max_agents = 1;
    c.h->app->orgs->put(limited);

    Json::Value pOut = approveProposal(c);
    std::string pid = pOut["proposal_id"].asString();
    vote(*c.h, c.creator, pid, "YES");

    auto p = c.h->app->proposals->get(c.orgId, pid);
    EXPECT_EQ(p->status, ProposalStatus::PASSED);
    auto m = c.h->app->orgs->getMembership(c.orgId, c.candidate.agent_id);
    EXPECT_EQ(m->status, MemberStatus::PENDING);
    EXPECT_EQ(c.h->app->indexes->getJoinLimit(c.orgId, 1700000000), 0);
}

TEST(Actions, UpdateOrgInfoAppliesAndReindexesAtomically) {
    ClosedOrg c = makeClosedOrg("Meta");
    Json::Value args;
    args["org_id"] = c.orgId;
    args["title"] = "Update profile";
    Json::Value action;
    action["kind"] = "UPDATE_ORG_INFO";
    Json::Value payload;
    payload["name"] = "Renamed Org";
    payload["category"] = "governance";
    Json::Value tags(Json::arrayValue);
    tags.append("gov");
    payload["tags"] = tags;
    payload["max_agents"] = 42;
    action["payload"] = payload;
    args["action"] = action;
    Json::Value pOut = c.h->call("create_proposal", args, &c.creator);
    std::string pid = pOut["proposal_id"].asString();
    vote(*c.h, c.creator, pid, "YES");

    AgentContext viewer = c.h->registerAgent("meta-viewer");
    Json::Value prof = getOrg(*c.h, c.orgId, viewer);
    ASSERT_TRUE(prof.isObject());
    EXPECT_EQ(prof["name"].asString(), "Renamed Org");
    EXPECT_EQ(prof["category"].asString(), "governance");
    EXPECT_EQ(prof["max_agents"].asInt64(), 42);

    Json::Value searchArgs;
    searchArgs["query"] = "renamed";
    Json::Value found = c.h->call("search_organizations", searchArgs, &viewer);
    EXPECT_EQ(found["items"].size(), 1u);
    searchArgs["query"] = "meta";
    found = c.h->call("search_organizations", searchArgs, &viewer);
    EXPECT_EQ(found["items"].size(), 0u);

    searchArgs.clear();
    searchArgs["category"] = "governance";
    found = c.h->call("search_organizations", searchArgs, &viewer);
    EXPECT_EQ(found["items"].size(), 1u);

    bool audited = false;
    for (const auto& e : c.h->app->audit->listByOrg(c.orgId)) {
        if (e.action == "ORG_INFO_UPDATED" || e.action == "CONFIG_CHANGED") audited = true;
    }
    EXPECT_TRUE(audited);
}

TEST(Actions, FailedCommitOnApproveMemberLeavesNoPartialStateAndRecoversOnce) {
    ClosedOrg c = makeClosedOrg("FailAppr");
    Json::Value pOut = approveProposal(c);
    ASSERT_FALSE(c.h->isError(pOut));
    std::string pid = pOut["proposal_id"].asString();

    c.h->app->db->beforeCommitHook = [] { return false; };
    Json::Value out = vote(*c.h, c.creator, pid, "YES");
    c.h->app->db->beforeCommitHook = nullptr;
    EXPECT_EQ(c.h->errorCode(out), -32603);
    EXPECT_FALSE(DbHealth::instance().healthy());
    DbHealth::instance().reset();

    auto m = c.h->app->orgs->getMembership(c.orgId, c.candidate.agent_id);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->status, MemberStatus::PENDING) << "активация не видна при отклонённом коммите";
    auto org = c.h->app->orgs->get(c.orgId);
    ASSERT_TRUE(org.has_value());
    EXPECT_DOUBLE_EQ(org->total_voting_power, 1.0) << "инкремент total_voting_power откатился вместе с батчем";
    EXPECT_EQ(c.h->app->indexes->getJoinLimit(c.orgId, 1700000000), 0);
    EXPECT_TRUE(c.h->app->indexes->hasPending(c.orgId, c.candidate.agent_id));
    auto p = c.h->app->proposals->get(c.orgId, pid);
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->status, ProposalStatus::ACTIVE) << "предложение осталось ACTIVE — закрытие атомарно отменено";
    EXPECT_FALSE(c.h->app->votes->get(c.orgId, pid, c.creator.agent_id).has_value());

    EXPECT_EQ(c.h->errorCode(vote(*c.h, c.creator, pid, "YES")), 0);
    org = c.h->app->orgs->get(c.orgId);
    ASSERT_TRUE(org.has_value());
    EXPECT_DOUBLE_EQ(org->total_voting_power, 2.0)
        << "повторное закрытие применяет эффект ровно один раз (без двойного инкремента)";
    EXPECT_EQ(c.h->app->indexes->getJoinLimit(c.orgId, 1700000000), 1);
}

TEST(Actions, FailedCommitOnConfigDeltaKeepsOrgConfigAndRecoversOnce) {
    auto h = Harness::create();
    AgentContext creator = h->registerAgent("faildelta-admin");
    Json::Value orgOut = createOrg(*h, creator, "FailDelta Org", "OPEN", orgConfigArgs("MAJORITY", 600));
    std::string orgId = orgOut["org_id"].asString();

    Json::Value pArgs;
    pArgs["org_id"] = orgId;
    pArgs["title"] = "Switch to CONSENT";
    pArgs["config_delta"] = orgConfigArgs("CONSENT", 5000);
    Json::Value pOut = h->call("create_proposal", pArgs, &creator);
    ASSERT_FALSE(h->isError(pOut));
    std::string pid = pOut["proposal_id"].asString();

    h->app->db->beforeCommitHook = [] { return false; };
    Json::Value out = vote(*h, creator, pid, "YES");
    h->app->db->beforeCommitHook = nullptr;
    EXPECT_EQ(h->errorCode(out), -32603);
    DbHealth::instance().reset();

    auto org = h->app->orgs->get(orgId);
    ASSERT_TRUE(org.has_value());
    EXPECT_EQ(org->config.consensus_model, ConsensusModel::MAJORITY) << "config_delta не применился при отказе";
    EXPECT_EQ(org->config.voting_duration_sec, 600);
    bool audited = false;
    for (const auto& e : h->app->audit->listByOrg(orgId)) {
        if (e.action == "CONFIG_CHANGED") audited = true;
    }
    EXPECT_FALSE(audited);

    EXPECT_EQ(h->errorCode(vote(*h, creator, pid, "YES")), 0) << "восстановление — повторный голос до PASSED";
    org = h->app->orgs->get(orgId);
    ASSERT_TRUE(org.has_value());
    EXPECT_EQ(org->config.consensus_model, ConsensusModel::CONSENT) << "восстановление применяет дельту";
    EXPECT_EQ(org->config.voting_duration_sec, 5000);
    int configChanged = 0;
    for (const auto& e : h->app->audit->listByOrg(orgId)) {
        if (e.action == "CONFIG_CHANGED") ++configChanged;
    }
    EXPECT_EQ(configChanged, 1) << "аудит ровно один CONFIG_CHANGED";
}

TEST(Actions, FailedCommitOnUpdateOrgInfoKeepsSearchConsistent) {
    ClosedOrg c = makeClosedOrg("FailMeta");
    Json::Value args;
    args["org_id"] = c.orgId;
    args["title"] = "Rename after failure";
    Json::Value action;
    action["kind"] = "UPDATE_ORG_INFO";
    Json::Value payload;
    payload["name"] = "Relabeled Org";
    Json::Value tags(Json::arrayValue);
    tags.append("fresh");
    payload["tags"] = tags;
    action["payload"] = payload;
    args["action"] = action;
    Json::Value pOut = c.h->call("create_proposal", args, &c.creator);
    ASSERT_FALSE(c.h->isError(pOut));
    std::string pid = pOut["proposal_id"].asString();

    c.h->app->db->beforeCommitHook = [] { return false; };
    Json::Value out = vote(*c.h, c.creator, pid, "YES");
    c.h->app->db->beforeCommitHook = nullptr;
    EXPECT_EQ(c.h->errorCode(out), -32603);
    DbHealth::instance().reset();

    AgentContext viewer = c.h->registerAgent("failmeta-viewer");
    Json::Value searchArgs;
    searchArgs["query"] = "relabeled";
    EXPECT_EQ(c.h->call("search_organizations", searchArgs, &viewer)["items"].size(), 0u)
        << "новое имя не появилось в поиске при отклонённом коммите";
    searchArgs["query"] = "failmeta";
    EXPECT_EQ(c.h->call("search_organizations", searchArgs, &viewer)["items"].size(), 1u)
        << "старые индексы согласованы с неизменённой организацией";

    EXPECT_EQ(c.h->errorCode(vote(*c.h, c.creator, pid, "YES")), 0)
        << "восстановление — повторный голос до PASSED";
    searchArgs["query"] = "relabeled";
    EXPECT_EQ(c.h->call("search_organizations", searchArgs, &viewer)["items"].size(), 1u)
        << "после восстановления поиск видит новое имя";
    searchArgs["query"] = "failmeta";
    EXPECT_EQ(c.h->call("search_organizations", searchArgs, &viewer)["items"].size(), 0u)
        << "после восстановления старое имя удалено из поиска";
}

// Спека consensus-engine «Лимит блокирует применение действия» +
// mcp-protocol «Лимит заблокировал применение действия»: карточка,
// событие proposal_closed и метрика согласованно говорят "не применено".
TEST(Actions, LimitBlockedApprovalKeepsCardEventAndMetricHonest) {
    ClosedOrg c = makeClosedOrg("Honest");
    auto orgBefore = c.h->app->orgs->get(c.orgId);
    Organization limited = *orgBefore;
    limited.max_agents = 1;
    c.h->app->orgs->put(limited);

    Json::Value pOut = approveProposal(c);
    ASSERT_FALSE(c.h->isError(pOut));
    std::string pid = pOut["proposal_id"].asString();

    const std::int64_t appliedBefore = counterTotal("voterpool_actions_applied_total");

    // Голосуем через записывающий движок, чтобы поймать proposal_closed.
    std::vector<SseEvent> events;
    ConsensusEngine recorder(ConsensusEngine::Deps{
        c.h->app->db.get(), c.h->app->orgs.get(), c.h->app->proposals.get(),
        c.h->app->votes.get(), c.h->app->indexes.get(), c.h->app->audit.get(),
        &c.h->app->locks, &c.h->app->orgLocks, &c.h->clock, c.h->app->orgNames.get(),
        nullptr,
        [&events](const SseEvent& ev) { events.push_back(ev); }});
    auto receipt = recorder.castVote(c.creator.agent_id, pid, VoteDecision::YES);
    ASSERT_TRUE(receipt.ok()) << receipt.error().message;

    // Событие: PASSED, но действие не применено.
    Json::Value closed = closedPayload(events);
    ASSERT_TRUE(closed.isObject());
    EXPECT_EQ(closed["final_status"].asString(), "PASSED");
    EXPECT_EQ(closed["config_delta_applied"].asBool(), false);
    EXPECT_TRUE(closed["action_applied"].isNull());

    // Хранимые флаги предложения — false.
    auto p = c.h->app->proposals->get(c.orgId, pid);
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->status, ProposalStatus::PASSED);
    EXPECT_FALSE(p->action_applied);
    EXPECT_FALSE(p->config_delta_applied);

    // Участник остался PENDING.
    auto m = c.h->app->orgs->getMembership(c.orgId, c.candidate.agent_id);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->status, MemberStatus::PENDING);

    // Карточка get_proposal согласована с событием.
    Json::Value cardArgs;
    cardArgs["proposal_id"] = pid;
    Json::Value card = c.h->call("get_proposal", cardArgs, &c.creator);
    ASSERT_FALSE(c.h->isError(card));
    EXPECT_TRUE(card["action_applied"].isNull());
    EXPECT_EQ(card["config_delta_applied"].asBool(), false);

    // Метрика применений не выросла.
    EXPECT_EQ(counterTotal("voterpool_actions_applied_total"), appliedBefore);
}

TEST(Actions, SuccessfulApproveReportsKindAndIncrementsMetricOnce) {
    ClosedOrg c = makeClosedOrg("Metric");
    const std::int64_t appliedBefore = counterTotal("voterpool_actions_applied_total");

    Json::Value first = approveProposal(c);
    std::string pid1 = first["proposal_id"].asString();
    vote(*c.h, c.creator, pid1, "YES");
    EXPECT_EQ(counterTotal("voterpool_actions_applied_total"), appliedBefore + 1);

    Json::Value cardArgs;
    cardArgs["proposal_id"] = pid1;
    Json::Value card = c.h->call("get_proposal", cardArgs, &c.creator);
    ASSERT_FALSE(c.h->isError(card));
    EXPECT_EQ(card["action_applied"].asString(), "APPROVE_MEMBER");
    EXPECT_EQ(card["config_delta_applied"].asBool(), false);

    auto p = c.h->app->proposals->get(c.orgId, pid1);
    ASSERT_TRUE(p.has_value());
    EXPECT_TRUE(p->action_applied);

    // Идемпотентный повтор по уже ACTIVE агенту: флаг false, метрика не растёт.
    const std::int64_t afterFirst = counterTotal("voterpool_actions_applied_total");
    Json::Value second = approveProposal(c);
    std::string pid2 = second["proposal_id"].asString();
    // CONSENT: теперь активных участников двое — нужны оба голоса для PASSED.
    vote(*c.h, c.creator, pid2, "YES");
    Json::Value receipt2 = vote(*c.h, c.candidate, pid2, "YES");
    ASSERT_EQ(receipt2["proposal_status"].asString(), "PASSED");

    auto p2 = c.h->app->proposals->get(c.orgId, pid2);
    ASSERT_TRUE(p2.has_value());
    EXPECT_EQ(p2->status, ProposalStatus::PASSED);
    EXPECT_FALSE(p2->action_applied) << "идемпотентный пропуск не есть применение";
    EXPECT_EQ(counterTotal("voterpool_actions_applied_total"), afterFirst);

    Json::Value cardArgs2;
    cardArgs2["proposal_id"] = pid2;
    Json::Value card2 = c.h->call("get_proposal", cardArgs2, &c.creator);
    ASSERT_FALSE(c.h->isError(card2));
    EXPECT_TRUE(card2["action_applied"].isNull());
}

TEST(Actions, UpdateOrgInfoCardReportsAppliedKind) {
    ClosedOrg c = makeClosedOrg("KindMeta");
    Json::Value args;
    args["org_id"] = c.orgId;
    args["title"] = "kind meta";
    Json::Value action;
    action["kind"] = "UPDATE_ORG_INFO";
    Json::Value payload;
    payload["description"] = "updated via consensus";
    action["payload"] = payload;
    args["action"] = action;
    Json::Value pOut = c.h->call("create_proposal", args, &c.creator);
    ASSERT_FALSE(c.h->isError(pOut));
    std::string pid = pOut["proposal_id"].asString();

    Json::Value receipt = vote(*c.h, c.creator, pid, "YES");
    ASSERT_EQ(receipt["proposal_status"].asString(), "PASSED");

    Json::Value cardArgs;
    cardArgs["proposal_id"] = pid;
    Json::Value card = c.h->call("get_proposal", cardArgs, &c.creator);
    ASSERT_FALSE(c.h->isError(card));
    EXPECT_EQ(card["action_applied"].asString(), "UPDATE_ORG_INFO");
    EXPECT_EQ(card["config_delta_applied"].asBool(), false);
}
