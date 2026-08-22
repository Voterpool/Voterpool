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

HttpResponse mcpPost(const Json::Value& body, const std::vector<std::pair<std::string, std::string>>& headers) {
    std::vector<std::pair<std::string, std::string>> all = {{"Content-Type", "application/json"}};
    for (const auto& h : headers) all.push_back(h);
    return HttpUtil::postJson(E2eEnv::instance().host(), E2eEnv::instance().port(), "/mcp", body, all);
}

Json::Value toolsCall(const std::string& name, const Json::Value& args,
                      const std::vector<std::pair<std::string, std::string>>& extra = {},
                      const char* headerNameOverride = nullptr) {
    Json::Value body;
    body["jsonrpc"] = "2.0";
    body["id"] = 1;
    body["method"] = "tools/call";
    body["params"]["name"] = name;
    body["params"]["arguments"] = args;
    std::vector<std::pair<std::string, std::string>> headers = extra;
    headers.push_back({"MCP-Protocol-Version", "2026-07-28"});
    headers.push_back({"Mcp-Method", "tools/call"});
    headers.push_back({"Mcp-Name", headerNameOverride ? headerNameOverride : name});
    return parseJson(mcpPost(body, headers).body);
}

bool isError(const Json::Value& envelope) { return envelope.isMember("error"); }
int errCode(const Json::Value& envelope) { return envelope["error"]["code"].asInt(); }

Json::Value unwrapResult(const Json::Value& envelope) {
    return parseJson(envelope["result"]["content"][0]["text"].asString());
}

Json::Value unwrapDirect(const Json::Value& envelope) {
    return parseJson(envelope["result"]["content"][0]["text"].asString());
}

}  // namespace

TEST(E2eProtocol, ServerDiscoverAnonymous) {
    Json::Value body;
    body["jsonrpc"] = "2.0";
    body["id"] = 0;
    body["method"] = "server/discover";
    HttpResponse resp = mcpPost(body, {{"MCP-Protocol-Version", "2026-07-28"},
                                       {"Mcp-Method", "tools/call"},
                                       {"Mcp-Name", "server/discover"}});
    ASSERT_EQ(resp.status, 200);
    Json::Value out = unwrapResult(parseJson(resp.body));
    EXPECT_EQ(out["protocolVersion"].asString(), "2026-07-28");
    EXPECT_EQ(out["extensions"]["io.voterpool/domain-events"]["endpoint"].asString(), "/mcp/events");
    EXPECT_EQ(out["serverInfo"]["name"].asString(), "voterpool");
}

TEST(E2eProtocol, ToolsListEnvelopeSortedAndComplete) {
    Json::Value body;
    body["jsonrpc"] = "2.0";
    body["id"] = 2;
    body["method"] = "tools/list";
    HttpResponse resp = mcpPost(body, {{"MCP-Protocol-Version", "2026-07-28"},
                                       {"Mcp-Method", "tools/call"},
                                       {"Mcp-Name", "tools/list"}});
    ASSERT_EQ(resp.status, 200);
    Json::Value result = unwrapResult(parseJson(resp.body));

    EXPECT_EQ(result["ttlMs"].asInt64(), 300000);
    EXPECT_EQ(result["cacheScope"].asString(), "server");

    const Json::Value& tools = result["tools"];
    ASSERT_TRUE(tools.isArray());
    EXPECT_GE(tools.size(), 15u);
    for (const auto& t : tools) {
        EXPECT_TRUE(t.isMember("description"));
        EXPECT_TRUE(t["inputSchema"].isObject());
    }
    std::string prev;
    for (const auto& t : tools) {
        std::string name = t["name"].asString();
        if (!prev.empty()) EXPECT_LT(prev, name) << "catalog must be lexicographically sorted";
        prev = name;
    }
}

TEST(E2eProtocol, MissingOrMismatchedHeadersRejected) {
    Json::Value args;
    args["name"] = "Headerless Agent";

    Json::Value mismatch = toolsCall("register_agent", args, {}, "wrong-name");
    EXPECT_TRUE(isError(mismatch));
    EXPECT_EQ(errCode(mismatch), -32600);

    Json::Value body;
    body["jsonrpc"] = "2.0";
    body["id"] = 1;
    body["method"] = "tools/call";
    body["params"]["name"] = "register_agent";
    body["params"]["arguments"] = args;
    HttpResponse noHeaders = mcpPost(body, {});
    ASSERT_EQ(noHeaders.status, 200);
    Json::Value parsed = parseJson(noHeaders.body);
    EXPECT_EQ(parsed["error"]["code"].asInt(), -32600);
}

TEST(E2eProtocol, UnknownToolMethodNotFound) {
    Json::Value out = toolsCall("definitely_not_a_tool", Json::Value(Json::objectValue));
    EXPECT_TRUE(isError(out));
    EXPECT_EQ(errCode(out), -32601);
}

TEST(E2eProtocol, DirectDeprecatedModeEquivalentToToolsCall) {
    Json::Value args;
    args["name"] = "Direct Mode Agent";

    Json::Value viaA = toolsCall("register_agent", args);
    ASSERT_FALSE(isError(viaA)) << viaA.toStyledString();
    Json::Value registered = unwrapResult(viaA);
    ASSERT_TRUE(registered.isMember("agent_id"));

    Json::Value directBody;
    directBody["jsonrpc"] = "2.0";
    directBody["id"] = "d1";
    directBody["method"] = "get_agent";
    directBody["params"]["agent_id"] = registered["agent_id"];

    HttpResponse resp = mcpPost(directBody, {{"MCP-Protocol-Version", "2026-07-28"},
                                             {"Mcp-Method", "tools/call"},
                                             {"Mcp-Name", "get_agent"},
                                             {"Authorization", "Bearer " + registered["api_key"].asString()}});
    ASSERT_EQ(resp.status, 200);
    Json::Value directOut = parseJson(resp.body);
    ASSERT_FALSE(directOut.isMember("error")) << directOut.toStyledString();
    Json::Value profile = unwrapResult(directOut);
    EXPECT_EQ(profile["agent_id"].asString(), registered["agent_id"].asString());
    EXPECT_EQ(profile["name"].asString(), "Direct Mode Agent");
}
