#include "tests/common/HttpUtil.h"
#include "tests/common/SseClient.h"
#include "tests/e2e/E2eEnv.h"

#include <gtest/gtest.h>

#include <atomic>
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
                 const std::vector<std::pair<std::string, std::string>>& extra = {}) {
    Json::Value body;
    body["jsonrpc"] = "2.0";
    body["id"] = 1;
    body["method"] = "tools/call";
    body["params"]["name"] = tool;
    body["params"]["arguments"] = args;
    std::vector<std::pair<std::string, std::string>> headers = {{"Content-Type", "application/json"},
                                                                {"MCP-Protocol-Version", "2026-07-28"},
                                                                {"Mcp-Method", "tools/call"},
                                                                {"Mcp-Name", tool}};
    for (const auto& kv : extra) headers.push_back(kv);
    return parseJson(
        HttpUtil::postJson(E2eEnv::instance().host(), E2eEnv::instance().port(), "/mcp", body, headers).body);
}

bool isError(const Json::Value& v) { return v.isMember("error"); }
Json::Value unwrap(const Json::Value& v) { return parseJson(v["result"]["content"][0]["text"].asString()); }

struct TestAgent {
    std::string id;
    std::string token;
    static TestAgent create(const std::string& name) {
        Json::Value args;
        args["name"] = name;
        Json::Value inner = unwrap(call("register_agent", args));
        return {inner["agent_id"].asString(), inner["api_key"].asString()};
    }
    std::vector<std::pair<std::string, std::string>> auth() const {
        return {{"Authorization", "Bearer " + token}};
    }
};

}  // namespace

TEST(E2eSse, IsolationAndHeartbeatAndEventDelivery) {
    TestAgent admin = TestAgent::create("sse-admin");
    TestAgent member = TestAgent::create("sse-member");
    TestAgent stranger = TestAgent::create("sse-stranger");

    Json::Value orgArgs;
    orgArgs["name"] = "Sse Org";
    orgArgs["type"] = "OPEN";
    orgArgs["config"] = [&] {
        Json::Value cfg;
        cfg["consensus_model"] = "CONSENT";
        cfg["voting_duration_sec"] = 3600;
        cfg["power_distribution"] = "EQUAL";
        return cfg;
    }();
    std::string orgId = unwrap(call("create_organization", orgArgs, admin.auth()))["org_id"].asString();

    Json::Value joinArgs;
    joinArgs["org_id"] = orgId;
    call("join_organization", joinArgs, member.auth());

    SseClient memberStream(E2eEnv::instance().host(), E2eEnv::instance().port(), member.token);
    SseClient strangerStream(E2eEnv::instance().host(), E2eEnv::instance().port(), stranger.token);
    ASSERT_TRUE(memberStream.connected());
    ASSERT_TRUE(strangerStream.connected());

    // Обязательные заголовки SSE-ответа (sse-events); имена заголовков
    // HTTP регистронезависимы, Drogon отдаёт их в нижнем регистре.
    {
        const std::string head = memberStream.responseHead();
        EXPECT_NE(head.find("200"), std::string::npos) << head;
        EXPECT_NE(head.find("content-type: text/event-stream"), std::string::npos) << head;
        EXPECT_NE(head.find("cache-control: no-cache"), std::string::npos) << head;
        EXPECT_NE(head.find("connection: keep-alive"), std::string::npos) << head;
        EXPECT_NE(head.find("x-accel-buffering: no"), std::string::npos) << head;
    }

    Json::Value pArgs;
    pArgs["org_id"] = orgId;
    pArgs["title"] = "SSE check";
    std::string pid = unwrap(call("create_proposal", pArgs, admin.auth()))["proposal_id"].asString();

    bool memberGotCreated = false;
    for (int i = 0; i < 10 && !memberGotCreated; ++i) {
        auto ev = memberStream.nextEvent(2000);
        if (!ev.has_value()) break;
        if (ev->event == "__keepalive__") continue;
        if (ev->event == "proposal_created") memberGotCreated = true;
    }
    ASSERT_TRUE(memberGotCreated);

    std::optional<SseClient::Event> strangerEv;
    for (int i = 0; i < 6; ++i) {
        strangerEv = strangerStream.nextEvent(400);
        if (!strangerEv.has_value()) break;
        if (strangerEv->event == "__keepalive__") { strangerEv.reset(); continue; }
        break;
    }
    ASSERT_FALSE(strangerEv.has_value()) << "isolation violated";

    Json::Value voteArgs;
    voteArgs["proposal_id"] = pid;
    voteArgs["decision"] = "YES";
    call("cast_vote", voteArgs, admin.auth());

    bool sawVoteCast = false;
    bool sawClosed = false;
    for (int i = 0; i < 10 && !sawVoteCast && !sawClosed; ++i) {
        auto ev = memberStream.nextEvent(2000);
        if (!ev.has_value()) break;
        if (ev->event == "__keepalive__") continue;
        if (ev->event == "vote_cast") sawVoteCast = true;
        else if (ev->event == "proposal_closed") sawClosed = true;
    }
    EXPECT_TRUE(sawVoteCast || sawClosed)
        << "vote_cast или proposal_closed должны дойти до подписчика";

    SseClient hbStream(E2eEnv::instance().host(), E2eEnv::instance().port(), stranger.token);
    ASSERT_TRUE(hbStream.connected());
    EXPECT_TRUE(hbStream.sawKeepAliveWithin(5000)) << "expected : keep-alive within ~1s heartbeat";
}
