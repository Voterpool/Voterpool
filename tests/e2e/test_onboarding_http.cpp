#include "tests/common/HttpUtil.h"
#include "tests/common/SseClient.h"
#include "tests/e2e/E2eEnv.h"

#include <gtest/gtest.h>

#include <chrono>
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

HttpResponse mcpPostRaw(const Json::Value& body,
                        const std::vector<std::pair<std::string, std::string>>& headers) {
    std::vector<std::pair<std::string, std::string>> all = {{"Content-Type", "application/json"}};
    for (const auto& kv : headers) all.push_back(kv);
    return HttpUtil::postJson(E2eEnv::instance().host(), E2eEnv::instance().port(), "/mcp", body, all);
}

// metaToken кладётся строго в params._meta.io.voterpool/auth.bearer (режим A);
// directMode=true отправляет deprecated режим B (method = имя инструмента).
Json::Value callWith(const std::string& tool, const Json::Value& args,
                     const std::vector<std::pair<std::string, std::string>>& extra = {},
                     bool directMode = false, const std::string& metaToken = "") {
    Json::Value body;
    body["jsonrpc"] = "2.0";
    body["id"] = 1;
    if (directMode) {
        body["method"] = tool;
        body["params"] = args;
    } else {
        body["method"] = "tools/call";
        body["params"]["name"] = tool;
        body["params"]["arguments"] = args;
        if (!metaToken.empty()) {
            body["params"]["_meta"]["io.voterpool/auth"]["bearer"] = metaToken;
        }
    }
    std::vector<std::pair<std::string, std::string>> headers = {{"MCP-Protocol-Version", "2026-07-28"},
                                                                {"Mcp-Method", "tools/call"},
                                                                {"Mcp-Name", tool}};
    for (const auto& kv : extra) headers.push_back(kv);
    return parseJson(mcpPostRaw(body, headers).body);
}

bool isError(const Json::Value& envelope) { return envelope.isMember("error"); }
int errCode(const Json::Value& envelope) { return envelope["error"]["code"].asInt(); }
Json::Value unwrap(const Json::Value& envelope) {
    return parseJson(envelope["result"]["content"][0]["text"].asString());
}

struct TestAgent {
    std::string id;
    std::string token;

    static TestAgent create(const std::string& name) {
        Json::Value args;
        args["name"] = name;
        Json::Value out = callWith("register_agent", args);
        EXPECT_FALSE(isError(out)) << out.toStyledString();
        Json::Value inner = unwrap(out);
        return TestAgent{inner["agent_id"].asString(), inner["api_key"].asString()};
    }

    std::vector<std::pair<std::string, std::string>> auth() const {
        return {{"Authorization", "Bearer " + token}};
    }
};

}  // namespace

TEST(E2eOnboarding, MetaAuthProtectedCallsAndHeaderPriority) {
    TestAgent a = TestAgent::create("meta-a");
    TestAgent b = TestAgent::create("meta-b");

    // Защищённый вызов без заголовка Authorization - только _meta.
    Json::Value selfArgs;
    selfArgs["agent_id"] = a.id;
    Json::Value profile = unwrap(callWith("get_agent", selfArgs, {}, false, a.token));
    EXPECT_EQ(profile["agent_id"].asString(), a.id);

    // create_organization через _meta.
    Json::Value orgArgs;
    orgArgs["name"] = "Meta Auth Org";
    orgArgs["type"] = "OPEN";
    Json::Value cfg;
    cfg["consensus_model"] = "MAJORITY";
    cfg["voting_duration_sec"] = 600;
    cfg["power_distribution"] = "EQUAL";
    orgArgs["config"] = cfg;
    Json::Value created =
        unwrap(callWith("create_organization", orgArgs, {}, false, a.token));
    EXPECT_FALSE(created["org_id"].asString().empty());

    // Невалидный _meta-токен -> -32001.
    Json::Value rejected = callWith("get_agent", selfArgs, {}, false, "voterpool_sec_invalid");
    EXPECT_TRUE(isError(rejected));
    EXPECT_EQ(errCode(rejected), -32001);

    // Приоритет заголовка: header B + _meta A -> личность B (запрос за B).
    Json::Value bSelf;
    bSelf["agent_id"] = b.id;
    Json::Value fromB = unwrap(callWith("get_agent", bSelf, b.auth(), false, a.token));
    EXPECT_EQ(fromB["agent_id"].asString(), b.id);
}

TEST(E2eOnboarding, GetPlaybookAnonymousAndModeBEquivalence) {
    // Анонимно, режим A, без Authorization и _meta.
    Json::Value viaA = callWith("get_playbook", Json::Value(Json::objectValue));
    ASSERT_FALSE(isError(viaA)) << viaA.toStyledString();
    Json::Value pb = unwrap(viaA);
    EXPECT_GT(pb["playbook"].asString().size(), 200u);
    EXPECT_EQ(pb["full_guide"].asString(), "docs/14-agent-playbook.md");

    // Deprecated режим B (method = имя инструмента) даёт тот же результат.
    Json::Value viaB = callWith("get_playbook", Json::Value(Json::objectValue), {}, true);
    ASSERT_FALSE(isError(viaB)) << viaB.toStyledString();
    EXPECT_EQ(unwrap(viaB)["playbook"].asString(), pb["playbook"].asString());

    // Плейбук объявлен в каталоге.
    Json::Value body;
    body["jsonrpc"] = "2.0";
    body["id"] = 2;
    body["method"] = "tools/list";
    HttpResponse resp = mcpPostRaw(body, {{"MCP-Protocol-Version", "2026-07-28"},
                                          {"Mcp-Method", "tools/call"},
                                          {"Mcp-Name", "tools/list"}});
    Json::Value catalog = unwrap(parseJson(resp.body));
    bool listed = false;
    for (const auto& t : catalog["tools"]) {
        if (t["name"].asString() == "get_playbook") listed = true;
    }
    EXPECT_TRUE(listed);
}

TEST(E2eOnboarding, JoinRequestedDeliveredToMembersOnlyForClosedOrgs) {
    TestAgent admin = TestAgent::create("jr-admin");
    TestAgent candidate = TestAgent::create("jr-candidate");

    Json::Value closedArgs;
    closedArgs["name"] = "Jr Closed Org";
    closedArgs["type"] = "CLOSED";
    closedArgs["config"] = [&] {
        Json::Value cfg;
        cfg["consensus_model"] = "MAJORITY";
        cfg["voting_duration_sec"] = 600;
        cfg["power_distribution"] = "EQUAL";
        return cfg;
    }();
    std::string closedId =
        unwrap(callWith("create_organization", closedArgs, admin.auth()))["org_id"].asString();

    SseClient adminStream(E2eEnv::instance().host(), E2eEnv::instance().port(), admin.token);
    ASSERT_TRUE(adminStream.connected());

    // Заявка в CLOSED -> подписанный участник получает join_requested.
    Json::Value joinArgs;
    joinArgs["org_id"] = closedId;
    Json::Value joined = unwrap(callWith("join_organization", joinArgs, candidate.auth()));
    EXPECT_EQ(joined["status"].asString(), "PENDING");

    bool sawJoinRequested = false;
    for (int i = 0; i < 10 && !sawJoinRequested; ++i) {
        auto ev = adminStream.nextEvent(2000);
        if (!ev.has_value()) break;
        if (ev->event == "__keepalive__") continue;
        if (ev->event == "join_requested") {
            Json::Value payload = parseJson(ev->data);
            EXPECT_EQ(payload["org_id"].asString(), closedId);
            EXPECT_EQ(payload["agent_id"].asString(), candidate.id);
            EXPECT_TRUE(payload.isMember("requested_at"));
            sawJoinRequested = true;
        }
    }
    ASSERT_TRUE(sawJoinRequested);

    // Идемпотентный повторный join НЕ дублирует событие.
    callWith("join_organization", joinArgs, candidate.auth());
    bool duplicated = false;
    for (int i = 0; i < 6 && !duplicated; ++i) {
        auto ev = adminStream.nextEvent(400);
        if (!ev.has_value()) break;
        if (ev->event == "__keepalive__") continue;
        if (ev->event == "join_requested" &&
            parseJson(ev->data)["agent_id"].asString() == candidate.id) {
            duplicated = true;
        }
    }
    EXPECT_FALSE(duplicated) << "idempotent rejoin must not re-emit join_requested";

    // OPEN-организация: только member_joined, без join_requested.
    TestAgent openAdmin = TestAgent::create("jr-open-admin");
    TestAgent openJoiner = TestAgent::create("jr-open-joiner");
    Json::Value openArgs;
    openArgs["name"] = "Jr Open Org";
    openArgs["type"] = "OPEN";
    openArgs["config"] = [&] {
        Json::Value cfg;
        cfg["consensus_model"] = "CONSENT";
        cfg["voting_duration_sec"] = 600;
        cfg["power_distribution"] = "EQUAL";
        return cfg;
    }();
    std::string openId =
        unwrap(callWith("create_organization", openArgs, openAdmin.auth()))["org_id"].asString();

    SseClient openStream(E2eEnv::instance().host(), E2eEnv::instance().port(), openAdmin.token);
    ASSERT_TRUE(openStream.connected());
    Json::Value openJoinArgs;
    openJoinArgs["org_id"] = openId;
    callWith("join_organization", openJoinArgs, openJoiner.auth());
    bool sawMemberJoined = false;
    bool sawForbiddenEvent = false;
    for (int i = 0; i < 10 && !sawMemberJoined; ++i) {
        auto ev = openStream.nextEvent(2000);
        if (!ev.has_value()) break;
        if (ev->event == "__keepalive__") continue;
        if (ev->event == "member_joined") sawMemberJoined = true;
        if (ev->event == "join_requested") sawForbiddenEvent = true;
    }
    EXPECT_TRUE(sawMemberJoined);
    EXPECT_FALSE(sawForbiddenEvent) << "OPEN join must not emit join_requested";
}

TEST(E2eOnboarding, FullAutonomousOnboardingMetaOnlyClient) {
    // Клиент M никогда не посылает заголовков: весь флоу на _meta-канале.
    TestAgent m = TestAgent::create("auto-meta-agent");
    TestAgent h = TestAgent::create("auto-header-admin");
    const std::string uniqueName = "Autonomy Org 776532";

    // 1. Плейбук читается самим агентом.
    Json::Value pb = unwrap(callWith("get_playbook", Json::Value(Json::objectValue)));
    EXPECT_FALSE(pb["playbook"].asString().empty());

    // 2. Профиль через _meta.
    Json::Value prof;
    prof["short_description"] = "autonomous onboarding e2e";
    Json::Value updated = unwrap(callWith("update_agent", prof, {}, false, m.token));
    EXPECT_EQ(updated["agent_id"].asString(), m.id);

    // 3. Header-админ создаёт CLOSED-организацию.
    Json::Value orgArgs;
    orgArgs["name"] = uniqueName;
    orgArgs["short_description"] = "closed consensus sandbox";
    orgArgs["type"] = "CLOSED";
    orgArgs["config"] = [&] {
        Json::Value cfg;
        cfg["consensus_model"] = "MAJORITY";
        cfg["quorum_percentage"] = 51;
        cfg["voting_duration_sec"] = 600;
        cfg["power_distribution"] = "EQUAL";
        return cfg;
    }();
    std::string orgId = unwrap(callWith("create_organization", orgArgs, h.auth()))["org_id"].asString();

    // 4. M находит организацию поиском и вступает -> PENDING.
    Json::Value searchArgs;
    searchArgs["query"] = uniqueName;
    Json::Value found = unwrap(callWith("search_organizations", searchArgs, {}, false, m.token));
    bool seen = false;
    for (const auto& item : found["items"]) {
        if (item["org_id"].asString() == orgId) seen = true;
    }
    ASSERT_TRUE(seen);

    Json::Value joinArgs;
    joinArgs["org_id"] = orgId;
    Json::Value joined = unwrap(callWith("join_organization", joinArgs, {}, false, m.token));
    ASSERT_EQ(joined["status"].asString(), "PENDING");

    // 5. H видит заявку (list_pending_members), выносит на консенсус, голосует.
    Json::Value pendArgs;
    pendArgs["org_id"] = orgId;
    Json::Value pendingList = unwrap(callWith("list_pending_members", pendArgs, h.auth()));
    ASSERT_EQ(pendingList.size(), 1u);
    EXPECT_EQ(pendingList[0]["agent_id"].asString(), m.id);

    Json::Value approveArgs;
    approveArgs["org_id"] = orgId;
    approveArgs["title"] = "Approve auto-meta agent";
    Json::Value action;
    action["kind"] = "APPROVE_MEMBER";
    Json::Value payload;
    payload["target_agent_id"] = m.id;
    action["payload"] = payload;
    approveArgs["action"] = action;
    std::string approvePid =
        unwrap(callWith("create_proposal", approveArgs, h.auth()))["proposal_id"].asString();
    Json::Value voteArgs;
    voteArgs["proposal_id"] = approvePid;
    voteArgs["decision"] = "YES";
    Json::Value receipt = unwrap(callWith("cast_vote", voteArgs, h.auth()));
    ASSERT_EQ(receipt["proposal_status"].asString(), "PASSED");

    // 6. M узнаёт об активации поллингом get_agent (poll-until-ACTIVE контракт).
    bool active = false;
    Json::Value selfArgs;
    selfArgs["agent_id"] = m.id;
    for (int attempt = 0; attempt < 20 && !active; ++attempt) {
        Json::Value selfProfile =
            unwrap(callWith("get_agent", selfArgs, {}, false, m.token));
        for (const auto& mem : selfProfile["organizations"]) {
            if (mem["org_id"].asString() == orgId && mem["status"].asString() == "ACTIVE") active = true;
        }
        if (!active) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ASSERT_TRUE(active) << "candidate must discover activation by polling get_agent";

    // 7. ACTIVE M создаёт предложение и голосует; H голосует YES -> PASSED.
    Json::Value pArgs;
    pArgs["org_id"] = orgId;
    pArgs["title"] = "First autonomous decision";
    std::int64_t tBeforeCreate = static_cast<std::int64_t>(::time(nullptr)) - 5;
    std::string pid = unwrap(callWith("create_proposal", pArgs, {}, false, m.token))["proposal_id"].asString();

    Json::Value myVote = voteArgs;
    myVote["proposal_id"] = pid;
    Json::Value mReceipt = unwrap(callWith("cast_vote", myVote, {}, false, m.token));
    EXPECT_EQ(mReceipt["proposal_status"].asString(), "ACTIVE");
    Json::Value hReceipt = unwrap(callWith("cast_vote", myVote, h.auth()));
    EXPECT_EQ(hReceipt["proposal_status"].asString(), "PASSED")
        << "синхронный ответ cast_vote несёт исход закрывающего голоса";

    // 8. Обнаружение исхода: updated_since + get_proposal с голосами.
    Json::Value sinceArgs;
    sinceArgs["org_id"] = orgId;
    sinceArgs["filter"] = "COMPLETED";
    sinceArgs["updated_since"] = tBeforeCreate;
    Json::Value completed = unwrap(callWith("get_proposals", sinceArgs, {}, false, m.token));
    bool foundClosed = false;
    for (const auto& p : completed) {
        if (p["proposal_id"].asString() == pid && p["status"].asString() == "PASSED") foundClosed = true;
    }
    ASSERT_TRUE(foundClosed) << completed.toStyledString();

    Json::Value cardArgs;
    cardArgs["proposal_id"] = pid;
    Json::Value card = unwrap(callWith("get_proposal", cardArgs, {}, false, m.token));
    EXPECT_EQ(card["status"].asString(), "PASSED");
    EXPECT_EQ(card["votes"].size(), 2u);
}

TEST(E2eOnboarding, IsolationAndErrorPaths) {
    TestAgent admin = TestAgent::create("iso-admin");
    TestAgent stranger = TestAgent::create("iso-stranger");

    Json::Value orgArgs;
    orgArgs["name"] = "Iso Closed Org";
    orgArgs["type"] = "CLOSED";
    orgArgs["config"] = [&] {
        Json::Value cfg;
        cfg["consensus_model"] = "MAJORITY";
        cfg["voting_duration_sec"] = 600;
        cfg["power_distribution"] = "EQUAL";
        return cfg;
    }();
    std::string orgId =
        unwrap(callWith("create_organization", orgArgs, admin.auth()))["org_id"].asString();

    // Не-участник: -32002 на list_pending_members.
    Json::Value pendArgs;
    pendArgs["org_id"] = orgId;
    Json::Value foreignPending = callWith("list_pending_members", pendArgs, stranger.auth());
    EXPECT_TRUE(isError(foreignPending));
    EXPECT_EQ(errCode(foreignPending), -32002);

    // Чужой стрим не получает события организации.
    SseClient strangerStream(E2eEnv::instance().host(), E2eEnv::instance().port(), stranger.token);
    ASSERT_TRUE(strangerStream.connected());
    Json::Value joinArgs;
    joinArgs["org_id"] = orgId;
    TestAgent candidate = TestAgent::create("iso-candidate");
    callWith("join_organization", joinArgs, candidate.auth());
    bool leaked = false;
    for (int i = 0; i < 6 && !leaked; ++i) {
        auto ev = strangerStream.nextEvent(300);
        if (!ev.has_value()) break;
        if (ev->event == "__keepalive__") continue;
        if (ev->event == "join_requested") leaked = true;
    }
    EXPECT_FALSE(leaked) << "isolation violated";

    // SSE GET без заголовка отклоняется (401); _meta на GET неприменим.
    HttpResponse anonSse =
        HttpUtil::get(E2eEnv::instance().host(), E2eEnv::instance().port(), "/mcp/events");
    EXPECT_EQ(anonSse.status, 401);
}
