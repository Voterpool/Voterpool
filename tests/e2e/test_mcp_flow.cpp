#include "tests/common/HttpUtil.h"
#include "tests/common/SseClient.h"
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

HttpResponse mcpPostRaw(const Json::Value& body,
                        const std::vector<std::pair<std::string, std::string>>& headers) {
    std::vector<std::pair<std::string, std::string>> all = {{"Content-Type", "application/json"}};
    for (const auto& kv : headers) all.push_back(kv);
    return HttpUtil::postJson(E2eEnv::instance().host(), E2eEnv::instance().port(), "/mcp", body, all);
}

Json::Value call(const std::string& tool, const Json::Value& args,
                 const std::vector<std::pair<std::string, std::string>>& extra = {}) {
    Json::Value body;
    body["jsonrpc"] = "2.0";
    body["id"] = 1;
    body["method"] = "tools/call";
    body["params"]["name"] = tool;
    body["params"]["arguments"] = args;
    std::vector<std::pair<std::string, std::string>> headers = {
        {"MCP-Protocol-Version", "2026-07-28"}, {"Mcp-Method", "tools/call"}, {"Mcp-Name", tool}};
    for (const auto& kv : extra) headers.push_back(kv);
    return parseJson(mcpPostRaw(body, headers).body);
}

bool isError(const Json::Value& envelope) { return envelope.isMember("error"); }

Json::Value unwrap(const Json::Value& envelope) {
    return parseJson(envelope["result"]["content"][0]["text"].asString());
}

struct TestAgent {
    std::string id;
    std::string token;

    static TestAgent create(const std::string& name) {
        Json::Value args;
        args["name"] = name;
        Json::Value out = call("register_agent", args);
        EXPECT_FALSE(isError(out)) << out.toStyledString();
        Json::Value inner = unwrap(out);
        return TestAgent{inner["agent_id"].asString(), inner["api_key"].asString()};
    }

    std::vector<std::pair<std::string, std::string>> auth() const {
        return {{"Authorization", "Bearer " + token}};
    }
};

}  // namespace

TEST(E2eFlow, FullAgentLifecycleThroughHttp) {
    E2eEnv::instance();
    TestAgent admin = TestAgent::create("flow-admin");
    TestAgent member = TestAgent::create("flow-member");
    TestAgent outsider = TestAgent::create("flow-outsider");

    Json::Value orgArgs;
    orgArgs["name"] = "Flow Org";
    orgArgs["short_description"] = "E2E lifecycle";
    orgArgs["type"] = "OPEN";
    Json::Value tags(Json::arrayValue);
    tags.append("flow");
    orgArgs["tags"] = tags;
    Json::Value cfg;
    cfg["consensus_model"] = "MAJORITY";
    cfg["quorum_percentage"] = 51;
    cfg["voting_duration_sec"] = 3600;
    cfg["power_distribution"] = "EQUAL";
    orgArgs["config"] = cfg;

    Json::Value orgOut = call("create_organization", orgArgs, admin.auth());
    ASSERT_FALSE(isError(orgOut)) << orgOut.toStyledString();
    Json::Value org = unwrap(orgOut);
    std::string orgId = org["org_id"].asString();
    EXPECT_EQ(org["role"].asString(), "ADMIN");
    EXPECT_DOUBLE_EQ(org["voting_power"].asDouble(), 1.0);

    Json::Value joinArgs;
    joinArgs["org_id"] = orgId;
    Json::Value joined = unwrap(call("join_organization", joinArgs, member.auth()));
    EXPECT_EQ(joined["status"].asString(), "ACTIVE");

    SseClient sse(E2eEnv::instance().host(), E2eEnv::instance().port(), member.token);
    ASSERT_TRUE(sse.connected());

    Json::Value pArgs;
    pArgs["org_id"] = orgId;
    pArgs["title"] = "Adopt the flow";
    std::string proposalId = unwrap(call("create_proposal", pArgs, admin.auth()))["proposal_id"].asString();
    ASSERT_FALSE(proposalId.empty());

    bool sawCreated = false;
    std::optional<SseClient::Event> createdEv;
    for (int i = 0; i < 15 && !sawCreated; ++i) {
        createdEv = sse.nextEvent(3000);
        if (!createdEv.has_value()) break;
        if (createdEv->event == "__keepalive__") continue;
        if (createdEv->event == "proposal_created" &&
            parseJson(createdEv->data)["proposal_id"].asString() == proposalId)
            sawCreated = true;
        else break;
    }
    ASSERT_TRUE(sawCreated);

    Json::Value voteArgs;
    voteArgs["proposal_id"] = proposalId;
    voteArgs["decision"] = "YES";
    Json::Value voteMember = call("cast_vote", voteArgs, member.auth());
    ASSERT_FALSE(isError(voteMember)) << voteMember.toStyledString();
    Json::Value receipt = unwrap(voteMember);

    Json::Value voteAdmin = call("cast_vote", voteArgs, admin.auth());
    Json::Value adminReceipt = unwrap(voteAdmin);
    EXPECT_EQ(adminReceipt["proposal_status"].asString(), "PASSED");

    bool sawVoteCast = false;
    bool sawClosed = false;
    for (int i = 0; i < 15 && !sawClosed; ++i) {
        auto ev = sse.nextEvent(3000);
        if (!ev.has_value()) break;
        if (ev->event == "__keepalive__") continue;
        if (ev->event == "vote_cast") sawVoteCast = true;
        else if (ev->event == "proposal_closed" &&
                 parseJson(ev->data)["final_status"].asString() == "PASSED")
            sawClosed = true;
    }
    EXPECT_TRUE(sawVoteCast);
    ASSERT_TRUE(sawClosed);

    Json::Value completedArgs;
    completedArgs["org_id"] = orgId;
    completedArgs["filter"] = "COMPLETED";
    Json::Value completed = unwrap(call("get_proposals", completedArgs, admin.auth()));
    bool foundClosed = false;
    for (const auto& p : completed) {
        if (p["proposal_id"].asString() == proposalId &&
            p["status"].asString() == "PASSED")
            foundClosed = true;
    }
    EXPECT_TRUE(foundClosed);
    EXPECT_GT(receipt["power_applied"].asDouble(), 0.0);

    Json::Value searchArgs;
    searchArgs["query"] = "flow org";
    Json::Value found = unwrap(call("search_organizations", searchArgs, outsider.auth()));
    bool seenInSearch = false;
    for (const auto& item : found["items"]) {
        if (item["org_id"].asString() == orgId) seenInSearch = true;
    }
    EXPECT_TRUE(seenInSearch);

    Json::Value leaveArgs;
    leaveArgs["org_id"] = orgId;
    Json::Value left = unwrap(call("leave_organization", leaveArgs, member.auth()));
    EXPECT_EQ(left["status"].asString(), "LEFT");

    Json::Value transferArgs;
    transferArgs["org_id"] = orgId;
    transferArgs["target_agent_id"] = member.id;
    Json::Value transferErr = call("transfer_admin", transferArgs, admin.auth());
    EXPECT_TRUE(isError(transferErr));

    Json::Value dissolveArgs;
    dissolveArgs["org_id"] = orgId;
    Json::Value dissolved = unwrap(call("dissolve_organization", dissolveArgs, admin.auth()));
    EXPECT_EQ(dissolved["status"].asString(), "DISSOLVED");

    Json::Value profileArgs;
    profileArgs["org_id"] = orgId;
    Json::Value profileAfter = unwrap(call("get_organization", profileArgs, outsider.auth()));
    EXPECT_EQ(profileAfter["status"].asString(), "DISSOLVED");

    Json::Value zombieArgs;
    zombieArgs["org_id"] = orgId;
    zombieArgs["title"] = "zombie";
    Json::Value zombie = call("create_proposal", zombieArgs, admin.auth());
    EXPECT_TRUE(isError(zombie));
    EXPECT_EQ(zombie["error"]["code"].asInt(), -32004);
}
