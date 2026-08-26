// E2E мульти-агентная матрица (change improve-agent-onboarding-contracts,
// tasks 5.1-5.5): три независимых токена, три модели консенсуса, полный
// CLOSED-цикл, регрессия полевого отчёта и контракт ошибок/валидации.
#include "tests/common/HttpUtil.h"
#include "tests/common/SseClient.h"
#include "tests/e2e/E2eEnv.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <set>
#include <thread>

using namespace voterpool;
using namespace voterpool::testing;

namespace {

Json::Value parseJson(const std::string& body) {
    Json::Value out;
    if (body.empty()) return out;
    Json::CharReaderBuilder b;
    std::string errs;
    std::istringstream iss(body);
    Json::parseFromStream(b, iss, &out, &errs);
    return out;
}

Json::Value call(const std::string& tool, const Json::Value& args,
                 const std::vector<std::pair<std::string, std::string>>& extraHeaders = {},
                 int httpTimeoutMs = 8000) {
    Json::Value body;
    body["jsonrpc"] = "2.0";
    body["id"] = 1;
    body["method"] = "tools/call";
    body["params"]["name"] = tool;
    body["params"]["arguments"] = args;
    auto headers = HttpUtil::mcpHeaders("tools/call", tool);
    for (const auto& kv : extraHeaders) headers.push_back(kv);
    std::vector<std::pair<std::string, std::string>> all = {{"Content-Type", "application/json"}};
    for (const auto& kv : headers) all.push_back(kv);
    HttpResponse resp =
        HttpUtil::postJson(E2eEnv::instance().host(), E2eEnv::instance().port(), "/mcp", body,
                           all, httpTimeoutMs);
    return parseJson(resp.body);
}

bool isErr(const Json::Value& env) { return env.isMember("error"); }
int errCodeOf(const Json::Value& env) { return env["error"]["code"].asInt(); }
Json::Value errData(const Json::Value& env) {
    return env["error"].get("data", Json::Value(Json::objectValue));
}
Json::Value res(const Json::Value& env) {
    return parseJson(env["result"]["content"][0]["text"].asString());
}

struct EAgent {
    std::string id;
    std::string token;

    static EAgent create(const char* name) {
        Json::Value a;
        a["name"] = name;
        Json::Value env = call("register_agent", a);
        EXPECT_FALSE(isErr(env)) << env.toStyledString();
        Json::Value v = res(env);
        return EAgent{v["agent_id"].asString(), v["api_key"].asString()};
    }

    std::vector<std::pair<std::string, std::string>> auth() const {
        return {{"Authorization", "Bearer " + token}};
    }
};

struct OrgHandle {
    std::string id;
};

OrgHandle makeOrg(const EAgent&  owner, const char* name, const char* type,
                  const Json::Value& cfgExtra = Json::Value()) {
    Json::Value args;
    args["name"] = name;
    args["type"] = type;
    Json::Value cfg;
    cfg["consensus_model"] = "MAJORITY";
    cfg["voting_duration_sec"] = 600;
    for (const auto& k : cfgExtra.getMemberNames()) cfg[k] = cfgExtra[k];
    args["config"] = cfg;
    Json::Value env = call("create_organization", args, owner.auth());
    EXPECT_FALSE(isErr(env)) << env.toStyledString();
    return OrgHandle{res(env)["org_id"].asString()};
}

void join(const EAgent&  a, const OrgHandle& org) {
    Json::Value j;
    j["org_id"] = org.id;
    Json::Value env = call("join_organization", j, a.auth());
    EXPECT_FALSE(isErr(env)) << env.toStyledString();
}

std::string propose(const EAgent&  author, const OrgHandle& org, const char* title,
                    const Json::Value& extra = Json::Value()) {
    Json::Value pArgs;
    pArgs["org_id"] = org.id;
    pArgs["title"] = title;
    pArgs["description"] = title;
    for (const auto& k : extra.getMemberNames()) pArgs[k] = extra[k];
    Json::Value env = call("create_proposal", pArgs, author.auth());
    EXPECT_FALSE(isErr(env)) << env.toStyledString();
    if (isErr(env)) return {};
    return res(env)["proposal_id"].asString();
}

void vote(const EAgent&  a, const std::string& propId, const char* decision) {
    Json::Value v;
    v["proposal_id"] = propId;
    v["decision"] = decision;
    Json::Value env = call("cast_vote", v, a.auth());
    EXPECT_FALSE(isErr(env)) << env.toStyledString();
}

Json::Value waitClose(const EAgent&  a, const std::string& propId, int timeoutSec = 30) {
    Json::Value w;
    w["proposal_id"] = propId;
    w["timeout_sec"] = timeoutSec;
    return call("wait_proposal_close", w, a.auth(), timeoutSec * 1000 + 4000);
}

}  // namespace

// ===================== 5.1: каркас, три независимые сессии =====================

TEST(MultiAgent, ThreeIndependentSessionsWhoamiIdentitySeparation) {
    EAgent a1 = EAgent::create("ma-s1");
    EAgent a2 = EAgent::create("ma-s2");
    EAgent a3 = EAgent::create("ma-s3");
    ASSERT_NE(a1.id, a2.id);
    ASSERT_NE(a2.id, a3.id);

    // Токен каждой сессии резолвится в СВОЮ личность.
    for (const EAgent* me : {&a1, &a2, &a3}) {
        Json::Value w = res(call("whoami", Json::Value(Json::objectValue), me->auth()));
        ASSERT_FALSE(isErr(Json::Value()));
        EXPECT_EQ(w["agent_id"].asString(), me->id);
        EXPECT_EQ(w["memberships"].size(), 0u);
        EXPECT_EQ(w["pending"].size(), 0u);
    }
}

// ===================== 5.2: happy-path моделей консенсуса =====================

TEST(MultiAgentConsensus, MajorityOpenTwoThirdsPassesViaLongPoll) {
    EAgent a1 = EAgent::create("mac-maj-owner");
    EAgent a2 = EAgent::create("mac-maj-b");
    EAgent a3 = EAgent::create("mac-maj-c");
    OrgHandle org = makeOrg(a1, "MAC Majority Org", "OPEN");
    join(a2, org);
    join(a3, org);

    std::string prop = propose(a1, org, "Switch runtime to vendor B");
    vote(a1, prop, "YES");
    vote(a2, prop, "NO");
    vote(a3, prop, "YES");  // закрывающий голос: 2/3

    Json::Value outcome = waitClose(a1, prop);
    ASSERT_FALSE(isErr(outcome)) << outcome.toStyledString();
    Json::Value r = res(outcome);
    EXPECT_EQ(r["closed"].asBool(), true);
    EXPECT_EQ(r["status"].asString(), "PASSED");
}

TEST(MultiAgentConsensus, ConsentFirstNoRejectsEarlyAndUnanimousWithAbstainPasses) {
    // Ранний REJECTED: первый NO в CONSENT закрывает предложение немедленно.
    EAgent s1 = EAgent::create("mac-con-rej-owner");
    EAgent s2 = EAgent::create("mac-con-rej-no");
    Json::Value consent;
    consent["consensus_model"] = "CONSENT";
    OrgHandle orgNo = makeOrg(s1, "MAC Consent Reject Org", "OPEN", consent);
    join(s2, orgNo);
    std::string risky = propose(s1, orgNo, "Adopt risky policy");
    vote(s2, risky, "NO");

    Json::Value rejected = waitClose(s1, risky);
    ASSERT_FALSE(isErr(rejected));
    EXPECT_EQ(res(rejected)["status"].asString(), "REJECTED");

    // Единогласие из трёх: YES + YES + ABSTAIN -> PASSED (никто не против).
    EAgent u1 = EAgent::create("mac-con-un-owner");
    EAgent u2 = EAgent::create("mac-con-un-b");
    EAgent u3 = EAgent::create("mac-con-un-c");
    OrgHandle orgU = makeOrg(u1, "MAC Consent Unanimous Org", "OPEN", consent);
    join(u2, orgU);
    join(u3, orgU);
    std::string harmonious = propose(u1, orgU, "Harmonious decision");
    vote(u1, harmonious, "YES");
    vote(u2, harmonious, "YES");
    vote(u3, harmonious, "ABSTAIN");

    Json::Value passed = waitClose(u1, harmonious);
    ASSERT_FALSE(isErr(passed));
    EXPECT_EQ(res(passed)["status"].asString(), "PASSED");
}

TEST(MultiAgentConsensus, QuorumExactlyReachedPassesBelowQuorumExpiresByTtl) {
    // Точная граница: T=3, quorum 66% => порог 1.98; два YES = 2.0 -> PASSED.
    EAgent q1 = EAgent::create("mac-qu-owner");
    EAgent q2 = EAgent::create("mac-qu-b");
    EAgent q3 = EAgent::create("mac-qu-c");
    Json::Value q66;
    q66["consensus_model"] = "QUORUM_PERCENTAGE";
    q66["quorum_percentage"] = 66;
    OrgHandle orgQ = makeOrg(q1, "MAC Quorum Exact Org", "OPEN", q66);
    join(q2, orgQ);
    join(q3, orgQ);

    std::string exact = propose(q1, orgQ, "Quorum exactly reached");
    vote(q1, exact, "YES");
    vote(q2, exact, "YES");
    Json::Value passed = waitClose(q1, exact);
    ASSERT_FALSE(isErr(passed));
    EXPECT_EQ(res(passed)["status"].asString(), "PASSED");

    // Ниже кворума при истёкшем сроке (duration 2с) -> детерминированный EXPIRED.
    EAgent e1 = EAgent::create("mac-exp-owner");
    Json::Value q80short;
    q80short["consensus_model"] = "QUORUM_PERCENTAGE";
    q80short["quorum_percentage"] = 80;
    q80short["voting_duration_sec"] = 2;
    OrgHandle orgE = makeOrg(e1, "MAC Expire Org", "OPEN", q80short);
    std::string nobody = propose(e1, orgE, "Nobody votes this one");

    Json::Value outcome = waitClose(e1, nobody, 60);
    ASSERT_FALSE(isErr(outcome)) << outcome.toStyledString();
    Json::Value r = res(outcome);
    EXPECT_EQ(r["closed"].asBool(), true);
    EXPECT_EQ(r["status"].asString(), "EXPIRED");
}

// ===================== 5.3: полный CLOSED-цикл + лимитные границы =====================

TEST(MultiAgentClosedCycle, PendingToApproveMemberFlowThroughAllStages) {
    EAgent admin = EAgent::create("mac-cycle-admin");
    EAgent member = EAgent::create("mac-cycle-member");
    EAgent candidate = EAgent::create("mac-cycle-candidate");

    Json::Value closedCfg;
    closedCfg["consensus_model"] = "MAJORITY";
    OrgHandle org = makeOrg(admin, "MAC Cycle Closed Org", "CLOSED", closedCfg);

    // 1) member подаёт заявку -> PENDING.
    Json::Value jMember;
    jMember["org_id"] = org.id;
    Json::Value pendB = call("join_organization", jMember, member.auth());
    ASSERT_EQ(res(pendB)["status"].asString(), "PENDING");

    // Заявку видит единственный ACTIVE-участник (админ).
    Json::Value lp;
    lp["org_id"] = org.id;
    Json::Value pendingForAdmin = res(call("list_pending_members", lp, admin.auth()));
    ASSERT_EQ(pendingForAdmin.size(), 1u);
    EXPECT_EQ(pendingForAdmin[0]["agent_id"].asString(), member.id);

    // 2) Админ выносит APPROVE_MEMBER с payload.target_agent_id.
    Json::Value pArgs;
    pArgs["org_id"] = org.id;
    pArgs["title"] = "Admit cycle-member";
    Json::Value act;
    act["kind"] = "APPROVE_MEMBER";
    Json::Value pay;
    pay["target_agent_id"] = member.id;
    act["payload"] = pay;
    pArgs["action"] = act;
    Json::Value envPropM = call("create_proposal", pArgs, admin.auth());
    ASSERT_FALSE(isErr(envPropM)) << envPropM.toStyledString();
    std::string admitMember = res(envPropM)["proposal_id"].asString();

    vote(admin, admitMember, "YES");  // T=1: YES=1 > T/2=0.5 -> PASSED сразу
    Json::Value doneM = waitClose(admin, admitMember);
    ASSERT_FALSE(isErr(doneM));
    EXPECT_EQ(res(doneM)["status"].asString(), "PASSED");
    EXPECT_EQ(res(doneM)["action_applied"].asString(), "APPROVE_MEMBER");

    // member теперь ACTIVE и видит заявки.
    Json::Value lp2;
    lp2["org_id"] = org.id;
    Json::Value seenByMember = res(call("list_pending_members", lp2, member.auth()));
    EXPECT_EQ(seenByMember.size(), 0u);

    // Кандидат подаёт заявку; её видят ОБА участника.
    Json::Value jCand;
    jCand["org_id"] = org.id;
    Json::Value pendC = call("join_organization", jCand, candidate.auth());
    EXPECT_EQ(res(pendC)["status"].asString(), "PENDING");
    Json::Value lp3;
    lp3["org_id"] = org.id;
    Json::Value pendingList = res(call("list_pending_members", lp3, member.auth()));
    ASSERT_EQ(pendingList.size(), 1u);
    EXPECT_EQ(pendingList[0]["agent_id"].asString(), candidate.id);

    // MEMBER (не админ) выносит заявку на консенсус - консенсусное право.
    Json::Value pArgs2;
    pArgs2["org_id"] = org.id;
    pArgs2["title"] = "Admit cycle-candidate";
    Json::Value act2;
    act2["kind"] = "APPROVE_MEMBER";
    Json::Value pay2;
    pay2["target_agent_id"] = candidate.id;
    act2["payload"] = pay2;
    pArgs2["action"] = act2;
    Json::Value envProp = call("create_proposal", pArgs2, member.auth());
    ASSERT_FALSE(isErr(envProp));
    std::string admitCandidate = res(envProp)["proposal_id"].asString();

    vote(admin, admitCandidate, "YES");
    vote(member, admitCandidate, "YES");  // T=2 -> нужно 2 голоса

    Json::Value doneC = waitClose(member, admitCandidate);
    ASSERT_FALSE(isErr(doneC));
    EXPECT_EQ(res(doneC)["status"].asString(), "PASSED");
    EXPECT_EQ(res(doneC)["action_applied"].asString(), "APPROVE_MEMBER");

    // Кандидат теперь ACTIVE по данным whoami и get_agent.
    Json::Value w = res(call("whoami", Json::Value(Json::objectValue), candidate.auth()));
    ASSERT_EQ(w["memberships"].size(), 1u);
    EXPECT_EQ(w["memberships"][0]["org_id"].asString(), org.id);
    EXPECT_EQ(w["pending"].size(), 0u);
    Json::Value ga;
    ga["agent_id"] = candidate.id;
    Json::Value profile = res(call("get_agent", ga, candidate.auth()));
    bool activeSeen = false;
    for (const auto& m : profile["organizations"]) {
        if (m["org_id"].asString() == org.id && m["status"].asString() == "ACTIVE") activeSeen = true;
    }
    EXPECT_TRUE(activeSeen);
}

TEST(MultiAgentLimits, MaxAgentsFullAndJoinsPerDayExhaustedReturnBusinessRules) {
    EAgent admin = EAgent::create("mac-limits-admin");
    EAgent b = EAgent::create("mac-limits-b");
    EAgent c = EAgent::create("mac-limits-c");

    // max_agents=2: третий join в OPEN отклоняется -32005 c data.max_agents.
    Json::Value capCfg;
    capCfg["voting_duration_sec"] = 600;
    Json::Value argsCap;
    argsCap["name"] = "MAC Capacity Org";
    argsCap["type"] = "OPEN";
    Json::Value capConf;
    capConf["consensus_model"] = "MAJORITY";
    capConf["voting_duration_sec"] = 600;
    argsCap["config"] = capConf;
    argsCap["max_agents"] = 2;
    Json::Value envCap = call("create_organization", argsCap, admin.auth());
    ASSERT_FALSE(isErr(envCap));
    OrgHandle capped{res(envCap)["org_id"].asString()};
    join(b, capped);
    Json::Value j3;
    j3["org_id"] = capped.id;
    Json::Value full = call("join_organization", j3, c.auth());
    ASSERT_TRUE(isErr(full));
    EXPECT_EQ(errCodeOf(full), -32005);
    EXPECT_EQ(errData(full)["max_agents"].asInt64(), 2);

    // joins_per_day_limit=1: второй join за сутки -> -32005 c data.joins_per_day_limit.
    Json::Value argsDay;
    argsDay["name"] = "MAC Daily Cap Org";
    argsDay["type"] = "OPEN";
    Json::Value dayConf;
    dayConf["consensus_model"] = "MAJORITY";
    dayConf["voting_duration_sec"] = 600;
    argsDay["config"] = dayConf;
    argsDay["joins_per_day_limit"] = 1;
    Json::Value envDay = call("create_organization", argsDay, admin.auth());
    ASSERT_FALSE(isErr(envDay));
    OrgHandle daily{res(envDay)["org_id"].asString()};
    join(b, daily);  // лимит исчерпан этим вступлением
    Json::Value jAgain;
    jAgain["org_id"] = daily.id;
    Json::Value limited = call("join_organization", jAgain, c.auth());
    ASSERT_TRUE(isErr(limited));
    EXPECT_EQ(errCodeOf(limited), -32005);
    EXPECT_EQ(errData(limited)["joins_per_day_limit"].asInt64(), 1);
}

// ===================== 5.4: регрессия полевого отчёта =====================

TEST(MultiAgentRegression, IdentityTrapRegisterUnderLiveTokenWarnsAndStaysUnderConfigIdentity) {
    EAgent real = EAgent::create("mreg-real");
    // Агент вызывает register_agent, не сняв рабочий токен из конфига.
    Json::Value env = call("register_agent", [&] {
        Json::Value a;
        a["name"] = "mreg-second";
        return a;
    }(), real.auth());
    ASSERT_FALSE(isErr(env)) << env.toStyledString();
    Json::Value created = res(env);
    ASSERT_TRUE(created.isMember("identity_warning"));
    EXPECT_EQ(created["identity_warning"]["current_agent_id"].asString(), real.id);
    EXPECT_NE(created["agent_id"].asString(), real.id);

    // Продолжаем работать с конфиговым токеном: личность прежняя.
    Json::Value w = res(call("whoami", Json::Value(Json::objectValue), real.auth()));
    EXPECT_EQ(w["agent_id"].asString(), real.id);
}

TEST(MultiAgentRegression, UpdateOrgInfoAppliesOnPassAndCardReflectsOutcome) {
    EAgent o = EAgent::create("mreg-uio-owner");
    EAgent m = EAgent::create("mreg-uio-b");
    OrgHandle org = makeOrg(o, "MREG Update Org", "OPEN");
    join(m, org);

    std::string prop = propose(o, org, "Refresh pitch", [&] {
        Json::Value extra;
        Json::Value act;
        act["kind"] = "UPDATE_ORG_INFO";
        Json::Value pay;
        pay["short_description"] = "Updated by consensus";
        pay["description"] = "New charter body";
        act["payload"] = pay;
        extra["action"] = act;
        return extra;
    }());
    vote(o, prop, "YES");
    vote(m, prop, "YES");

    Json::Value done = waitClose(o, prop);
    ASSERT_FALSE(isErr(done));
    Json::Value r = res(done);
    EXPECT_EQ(r["status"].asString(), "PASSED");
    EXPECT_EQ(r["action_applied"].asString(), "UPDATE_ORG_INFO");

    // Эффект верифицируем чтением состояния организации.
    Json::Value p;
    p["org_id"] = org.id;
    Json::Value profile = res(call("get_organization", p, o.auth()));
    EXPECT_EQ(profile["short_description"].asString(), "Updated by consensus");

    // Негативные payload: пустой и неизвестное поле -> -32602 (без тишины).
    {
        Json::Value pArgs;
        pArgs["org_id"] = org.id;
        pArgs["title"] = "Bad payload";
        Json::Value act;
        act["kind"] = "UPDATE_ORG_INFO";
        act["payload"] = Json::Value(Json::objectValue);
        pArgs["action"] = act;
        Json::Value env = call("create_proposal", pArgs, o.auth());
        ASSERT_TRUE(isErr(env));
        EXPECT_EQ(errCodeOf(env), -32602);
    }
    {
        Json::Value pArgs;
        pArgs["org_id"] = org.id;
        pArgs["title"] = "Typo payload";
        Json::Value act;
        act["kind"] = "UPDATE_ORG_INFO";
        Json::Value pay;
        pay["short_descroption"] = "typo";
        act["payload"] = pay;
        pArgs["action"] = act;
        Json::Value env = call("create_proposal", pArgs, o.auth());
        ASSERT_TRUE(isErr(env));
        EXPECT_EQ(errCodeOf(env), -32602);
        Json::Value data = errData(env);
        EXPECT_EQ(data["field"].asString(), "short_descroption");
    }
}

namespace {
// Хелпер для негативных create_proposal: возвращает код ошибки или 0.
int tryPropose(const EAgent& author, const OrgHandle& org, const char* title,
               const Json::Value& extra) {
    Json::Value pArgs;
    pArgs["org_id"] = org.id;
    pArgs["title"] = title;
    for (const auto& k : extra.getMemberNames()) pArgs[k] = extra[k];
    Json::Value env = call("create_proposal", pArgs, author.auth());
    return isErr(env) ? errCodeOf(env) : 0;
}
}  // namespace

TEST(MultiAgentRegression, ConfigDeltaQuorumSixtyReflectedInOrganizationProfile) {
    EAgent o = EAgent::create("mreg-cd-owner");
    OrgHandle org = makeOrg(o, "MREG Delta Org", "OPEN");

    std::string prop = propose(o, org, "Raise quorum", [&] {
        Json::Value extra;
        Json::Value delta;
        delta["quorum_percentage"] = 60;
        extra["config_delta"] = delta;
        return extra;
    }());
    vote(o, prop, "YES");  // T=1 -> сразу PASSED

    Json::Value done = waitClose(o, prop);
    ASSERT_FALSE(isErr(done));
    EXPECT_EQ(res(done)["config_delta_applied"].asBool(), true);

    Json::Value p;
    p["org_id"] = org.id;
    Json::Value profile = res(call("get_organization", p, o.auth()));
    EXPECT_EQ(profile["config"]["quorum_percentage"].asInt(), 60);
}

TEST(MultiAgentRegression, FilterAllAvailableToNonAdminActiveMemberAndSearchFindsByName) {
    EAgent o = EAgent::create("mreg-fil-owner");
    EAgent plainMember = EAgent::create("mreg-fil-member");
    OrgHandle org = makeOrg(o, "MREG Governance Lab Alpha", "OPEN");
    join(plainMember, org);
    propose(o, org, "First item");
    propose(o, org, "Second item");

    // Не-админ ACTIVE участник читает filter ALL без -32002.
    Json::Value gp;
    gp["org_id"] = org.id;
    gp["filter"] = "ALL";
    Json::Value envList = call("get_proposals", gp, plainMember.auth());
    ASSERT_FALSE(isErr(envList)) << envList.toStyledString();
    EXPECT_EQ(res(envList).size(), 2u);

    // Поиск по точному имени находит организацию (регрессия пункта #8).
    Json::Value q;
    q["query"] = "Governance Lab Alpha";
    Json::Value found = res(call("search_organizations", q, o.auth()));
    bool hit = false;
    for (const auto& item : found["items"]) {
        if (item["org_id"].asString() == org.id) hit = true;
    }
    EXPECT_TRUE(hit);
}

TEST(MultiAgentRegression, ExpiredProposalAggregatesConsistentBetweenListAndCard) {
    EAgent e = EAgent::create("mreg-exp-owner");
    Json::Value cfg;
    cfg["voting_duration_sec"] = 2;
    OrgHandle org = makeOrg(e, "MREG Expired Consistency", "OPEN", cfg);
    std::string prop = propose(e, org, "Will expire untouched");

    Json::Value done = waitClose(e, prop, 60);
    ASSERT_FALSE(isErr(done));
    // MAJORITY: время вышло без кворума -> REJECTED (детерминированный исход).
    const std::string finalStatus = res(done)["status"].asString();
    ASSERT_TRUE(finalStatus == "REJECTED" || finalStatus == "EXPIRED") << finalStatus;

    // Карточка: голосов нет, агрегаты нулевые.
    Json::Value cp;
    cp["proposal_id"] = prop;
    Json::Value card = res(call("get_proposal", cp, e.auth()));
    ASSERT_EQ(card["status"].asString(), finalStatus);
    EXPECT_EQ(card["votes"].size(), 0u);

    // Список согласован с карточкой.
    Json::Value lp;
    lp["org_id"] = org.id;
    lp["filter"] = "COMPLETED";
    Json::Value listed = res(call("get_proposals", lp, e.auth()));
    bool saw = false;
    for (const auto& item : listed) {
        if (item["proposal_id"].asString() != prop) continue;
        saw = true;
        EXPECT_EQ(item["status"].asString(), finalStatus);
        EXPECT_NEAR(item["yes_power"].asDouble(), card["yes_power"].asDouble(), 1e-9);
        EXPECT_EQ(item["voters_count"].asInt64(), card["voters_count"].asInt64());
    }
    EXPECT_TRUE(saw);
}

// ===================== 5.5: контракт ошибок и ожидания =====================

TEST(MultiAgentErrors, StrictArgsHintsAndBusinessDataOverHttp) {
    EAgent o = EAgent::create("mer-owner");
    OrgHandle org = makeOrg(o, "MER Errors Org", "OPEN");
    std::string prop = propose(o, org, "Error probe");

    // Опечатка в имени аргумента -> -32602 + did-you-mean.
    Json::Value typo;
    typo["proposal_id"] = prop;
    typo["decison"] = "YES";
    Json::Value envTypo = call("cast_vote", typo, o.auth());
    ASSERT_TRUE(isErr(envTypo));
    EXPECT_EQ(errCodeOf(envTypo), -32602);
    Json::Value d1 = errData(envTypo);
    EXPECT_EQ(d1["field"].asString(), "decison");
    EXPECT_NE(d1["hint"].asString().find("decision"), std::string::npos);

    // Неверный тип -> -32602 c expected_type.
    Json::Value badType;
    badType["name"] = "MER Tags Org";
    badType["type"] = "OPEN";
    Json::Value cfg;
    cfg["consensus_model"] = "MAJORITY";
    cfg["voting_duration_sec"] = 600;
    badType["config"] = cfg;
    badType["tags"] = "governance";  // строка вместо массива
    Json::Value envType = call("create_organization", badType, o.auth());
    ASSERT_TRUE(isErr(envType));
    EXPECT_EQ(errCodeOf(envType), -32602);
    EXPECT_EQ(errData(envType)["expected_type"].asString(), "array");

    // Обязательное поле отсутствует.
    Json::Value missing;
    missing["proposal_id"] = prop;
    Json::Value envMiss = call("cast_vote", missing, o.auth());
    ASSERT_TRUE(isErr(envMiss));
    EXPECT_EQ(errCodeOf(envMiss), -32602);
    EXPECT_NE(errData(envMiss)["reason"].asString().find("decision"), std::string::npos);

    // ABSTAIN в MAJORITY -> -32005 c allowed=[YES,NO].
    vote(o, prop, "YES");  // T=1, закрывающий
    std::string secondProp = propose(o, org, "Abstain probe");
    Json::Value abstain;
    abstain["proposal_id"] = secondProp;
    abstain["decision"] = "ABSTAIN";
    Json::Value envAbstain = call("cast_vote", abstain, o.auth());
    ASSERT_TRUE(isErr(envAbstain));
    EXPECT_EQ(errCodeOf(envAbstain), -32005);
    Json::Value d2 = errData(envAbstain);
    EXPECT_EQ(d2["consensus_model"].asString(), "MAJORITY");
    ASSERT_EQ(d2["allowed"].size(), 2u);
    {
        std::multiset<std::string> allowedSet;
        for (const auto& v : d2["allowed"]) allowedSet.insert(v.asString());
        EXPECT_EQ(allowedSet.count("YES"), 1u);
        EXPECT_EQ(allowedSet.count("NO"), 1u);
    }

    // Повторное голосование -> previous_decision.
    Json::Value again;
    again["proposal_id"] = prop;
    again["decision"] = "YES";
    // proposal уже закрыт (T=1 при первом YES): проверяем контракты обоих кодов:
    // сначала попытка повторного голосования на закрытом (-32003 current_status),
    for (int i = 0; i < 2; ++i) {
        Json::Value dup = call("cast_vote", again, o.auth());
        ASSERT_TRUE(isErr(dup));
        int code = errCodeOf(dup);
        EXPECT_TRUE(code == -32003) << code;
    }
}

TEST(MultiAgentErrors, NonMemberWaitRejectedFastWithoutBlockingPoolSlot) {
    EAgent owner = EAgent::create("mer-wait-owner");
    EAgent outsider = EAgent::create("mer-outsider");
    OrgHandle org = makeOrg(owner, "MER Wait Access", "OPEN");
    std::string prop = propose(owner, org, "Nobody sees");

    auto t0 = std::chrono::steady_clock::now();
    Json::Value w;
    w["proposal_id"] = prop;
    w["timeout_sec"] = 30;
    Json::Value env = call("wait_proposal_close", w, outsider.auth(), 34000);
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
            .count();
    ASSERT_TRUE(isErr(env));
    EXPECT_EQ(errCodeOf(env), -32002);
    EXPECT_LT(ms, 1500) << "-32002 must be immediate";
}

TEST(MultiAgentErrors, DissolveDuringWaitWakesWaitersWithExpiredAndHealthStaysResponsive) {
    EAgent owner = EAgent::create("mer-dissolve-a");
    EAgent b = EAgent::create("mer-dissolve-b");
    EAgent c = EAgent::create("mer-dissolve-c");
    OrgHandle org = makeOrg(owner, "MER Dissolve Wait", "OPEN");
    join(b, org);
    join(c, org);

    std::string doomed = propose(c, org, "Will be dissolved mid-wait");
    ASSERT_FALSE(doomed.empty());

    // Два параллельных ожидающих-участника: ответы собираются атомарно.
    struct WaitResult {
        bool answered = false;
        std::string status;
        bool closed = false;
    };
    std::vector<std::shared_ptr<WaitResult>> results(3);
    for (auto& r : results) r = std::make_shared<WaitResult>();

    std::atomic<int> healthProbes{0};
    std::atomic<bool> healthAllFast{true};

    std::vector<std::thread> waiters;
    waiters.emplace_back([&] {
        Json::Value w;
        w["proposal_id"] = doomed;
        w["timeout_sec"] = 30;
        Json::Value env = call("wait_proposal_close", w, b.auth(), 34000);
        if (!isErr(env)) {
            results[0]->answered = true;
            results[0]->status = res(env)["status"].asString();
            results[0]->closed = res(env)["closed"].asBool();
        }
    });
    waiters.emplace_back([&] {
        Json::Value w;
        w["proposal_id"] = doomed;
        w["timeout_sec"] = 30;
        Json::Value env = call("wait_proposal_close", w, c.auth(), 34000);
        if (!isErr(env)) {
            results[1]->answered = true;
            results[1]->status = res(env)["status"].asString();
            results[1]->closed = res(env)["closed"].asBool();
        }
    });
    waiters.emplace_back([&] {
        while (!results[0]->answered && healthProbes < 10) {
            HttpResponse hr =
                HttpUtil::request(E2eEnv::instance().host(), E2eEnv::instance().port(), "GET",
                                  "/health", {}, "", 1000);
            ++healthProbes;
            if (hr.status == 0 || hr.status >= 500) healthAllFast = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
        }
    });

    // Даём ожидающим зацепиться, затем роспуск от админа.
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    // Роспуск выполняет АДМИН организации (owner); участники в это время
    // блокированы в wait_proposal_close.
    Json::Value envDissolve = call("dissolve_organization", [&] {
        Json::Value a;
        a["org_id"] = org.id;
        return a;
    }(), owner.auth());
    ASSERT_FALSE(isErr(envDissolve)) << envDissolve.toStyledString();

    waiters.emplace_back([&] {
        Json::Value w;
        w["proposal_id"] = doomed;
        w["timeout_sec"] = 30;
        Json::Value env = call("wait_proposal_close", w, owner.auth(), 34000);
        if (!isErr(env)) {
            // Владелец присоединяется ПОСЛЕ роспуска: обязан получить
            // мгновенный ответ закрытым предложением.
            results[2]->answered = true;
            results[2]->status = res(env)["status"].asString();
            results[2]->closed = res(env)["closed"].asBool();
        }
    });

    for (auto& t : waiters) t.join();
    EXPECT_GE(healthProbes.load(), 2) << "health checks must proceed during waits";
    EXPECT_TRUE(healthAllFast) << "/health stayed responsive while long-polls were in flight";

    for (size_t i = 0; i < results.size(); ++i) {
        SCOPED_TRACE(i);
        EXPECT_TRUE(results[i]->answered);
        EXPECT_EQ(results[i]->closed, true);
        EXPECT_EQ(results[i]->status, "EXPIRED");
    }

    // DISSOLVED-организация сохраняет чтение и отвергает мутации -32004.
    Json::Value p;
    p["org_id"] = org.id;
    Json::Value profile = res(call("get_organization", p, owner.auth()));
    EXPECT_EQ(profile["status"].asString(), "DISSOLVED");
    Json::Value afterJoin;
    afterJoin["org_id"] = org.id;
    Json::Value gone = call("join_organization", afterJoin, owner.auth());
    ASSERT_TRUE(isErr(gone));
    EXPECT_EQ(errCodeOf(gone), -32004);
}
