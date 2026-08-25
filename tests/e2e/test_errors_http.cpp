#include "tests/common/HttpUtil.h"
#include "tests/e2e/E2eEnv.h"

#include <gtest/gtest.h>

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
int errCode(const Json::Value& v) { return v["error"]["code"].asInt(); }

struct TestAgent {
    static TestAgent create(const std::string& name) {
        Json::Value args;
        args["name"] = name;
        Json::Value out = call("register_agent", args);
        Json::Value inner = parseJson(out["result"]["content"][0]["text"].asString());
        TestAgent a{inner["agent_id"].asString(), inner["api_key"].asString()};
        return TestAgent{a.id, a.token};
    }
    std::string id;
    std::string token;
    std::vector<std::pair<std::string, std::string>> auth() const {
        return {{"Authorization", "Bearer " + token}};
    }
};

}  // namespace

TEST(E2eErrors, FullErrorCatalogOverHttp) {
    TestAgent a = TestAgent::create("errors-agent");
    TestAgent b = TestAgent::create("errors-other");

    EXPECT_EQ(errCode(call("create_organization", {}, {})), -32001);

    EXPECT_EQ(a.token.empty() ? 0 : errCode(call("get_proposals",
                                                 [&] {
                                                     Json::Value x;
                                                     x["org_id"] = "not-uuid";
                                                     return x;
                                                 }(),
                                                 a.auth())),
              -32602);

    EXPECT_EQ(errCode(call("cast_vote",
                           [&] {
                               Json::Value x;
                               x["proposal_id"] = "00000000-0000-4000-8000-000000000000";
                               x["decision"] = "YES";
                               return x;
                           }(),
                           a.auth())),
              -32004);

    Json::Value orgOut = call("create_organization",
                              [&] {
                                  Json::Value x;
                                  x["name"] = "Errors Org";
                                  x["type"] = "OPEN";
                                  Json::Value cfg;
                                  cfg["consensus_model"] = "MAJORITY";
                                  cfg["voting_duration_sec"] = 600;
                                  x["config"] = cfg;
                                  return x;
                              }(),
                              a.auth());
    ASSERT_FALSE(isError(orgOut));
    std::string orgId = parseJson(orgOut["result"]["content"][0]["text"].asString())["org_id"].asString();

    EXPECT_EQ(errCode(call("create_proposal",
                           [&] {
                               Json::Value x;
                               x["org_id"] = orgId;
                               x["title"] = "intruder";
                               return x;
                           }(),
                           b.auth())),
              -32002);

    EXPECT_EQ(errCode(call("update_voting_power",
                           [&] {
                               Json::Value x;
                               x["org_id"] = orgId;
                               x["target_agent_id"] = b.id;
                               x["new_power"] = 5.0;
                               return x;
                           }(),
                           b.auth())),
              -32002);

    Json::Value pArgs;
    pArgs["org_id"] = orgId;
    pArgs["title"] = "err-proposal";
    std::string pid =
        parseJson(call("create_proposal", pArgs, a.auth())["result"]["content"][0]["text"].asString())
            ["proposal_id"]
                .asString();

    Json::Value voteArgs;
    voteArgs["proposal_id"] = pid;
    voteArgs["decision"] = "YES";
    call("cast_vote", voteArgs, a.auth());

    Json::Value closedVote = call("cast_vote", voteArgs, a.auth());
    if (!isError(closedVote)) {
        GTEST_NONFATAL_FAILURE_("expected duplicate/closed conflict");
    } else {
        int code = errCode(closedVote);
        EXPECT_TRUE(code == -32003 || code == -32005) << code;
    }
}

TEST(E2eErrors, ParseErrorMapsToHttp400) {
    HttpResponse resp = HttpUtil::request(E2eEnv::instance().host(), E2eEnv::instance().port(), "POST",
                                          "/mcp",
                                          {{"Content-Type", "application/json"},
                                           {"MCP-Protocol-Version", "2026-07-28"},
                                           {"Mcp-Method", "tools/call"},
                                           {"Mcp-Name", "x"}},
                                          "{not valid json");
    EXPECT_EQ(resp.status, 400);
    Json::Value out = parseJson(resp.body);
    EXPECT_EQ(out["error"]["code"].asInt(), -32700);
    EXPECT_TRUE(out["id"].isNull());
}

TEST(E2eErrors, BrokenBodyWithAnyMcpMethodReachesHandlerAsParseError) {
    // Неразборчивое тело с Mcp-Method: server/discover не режется middleware
    // как -32600: parse error доходит до хендлера (-32700 + HTTP 400).
    HttpResponse resp = HttpUtil::request(E2eEnv::instance().host(), E2eEnv::instance().port(), "POST",
                                          "/mcp",
                                          {{"Content-Type", "application/json"},
                                           {"MCP-Protocol-Version", "2026-07-28"},
                                           {"Mcp-Method", "server/discover"}},
                                          "{{{not-json");
    EXPECT_EQ(resp.status, 400);
    Json::Value out = parseJson(resp.body);
    EXPECT_EQ(out["error"]["code"].asInt(), -32700);
    EXPECT_TRUE(out["id"].isNull());
}

TEST(E2eErrors, HeaderViolationsStillRejectedWithInvalidRequest) {
    // Валидный JSON с нарушением заголовков по-прежнему отклоняется на уровне
    // протокола (-32600), это не parse error.
    Json::Value body;
    body["jsonrpc"] = "2.0";
    body["id"] = 7;
    body["method"] = "server/discover";
    HttpResponse resp = HttpUtil::postJson(E2eEnv::instance().host(), E2eEnv::instance().port(), "/mcp",
                                           body,
                                           {{"Content-Type", "application/json"},
                                            {"MCP-Protocol-Version", "2026-07-28"},
                                            {"Mcp-Method", "tools/list"}});
    EXPECT_EQ(resp.status, 200);
    Json::Value out = parseJson(resp.body);
    EXPECT_TRUE(out.isMember("error"));
    EXPECT_EQ(out["error"]["code"].asInt(), -32600);
}

TEST(E2eErrors, HealthAndMetricsAreAnonymous) {
    HttpResponse health = HttpUtil::get(E2eEnv::instance().host(), E2eEnv::instance().port(), "/health");
    EXPECT_EQ(health.status, 200);

    HttpResponse metrics = HttpUtil::get(E2eEnv::instance().host(), E2eEnv::instance().port(), "/metrics");
    ASSERT_EQ(metrics.status, 200);
    EXPECT_NE(metrics.body.find("voterpool_mcp_requests_total"), std::string::npos);
    EXPECT_NE(metrics.body.find("# TYPE"), std::string::npos);
    EXPECT_EQ(metrics.headers.count("Content-Length") > 0 || !metrics.body.empty(), true);

    EXPECT_EQ(metrics.body.find("agent_id=\""), std::string::npos) << "cardinality discipline violated";
}
