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

Json::Value rpcBody(const char* method, const Json::Value& params, int id = 1) {
    Json::Value body;
    body["jsonrpc"] = "2.0";
    body["id"] = id;
    body["method"] = method;
    if (!params.isNull()) body["params"] = params;
    return body;
}

Json::Value toolsCall(const std::string& name, const Json::Value& args,
                      const std::vector<std::pair<std::string, std::string>>& extra = {},
                      const char* headerNameOverride = nullptr) {
    Json::Value params;
    params["name"] = name;
    params["arguments"] = args;
    auto headers = HttpUtil::mcpHeaders("tools/call", headerNameOverride ? headerNameOverride : name);
    for (const auto& h : extra) headers.push_back(h);
    return parseJson(mcpPost(rpcBody("tools/call", params), headers).body);
}

bool isError(const Json::Value& envelope) { return envelope.isMember("error"); }
int errCode(const Json::Value& envelope) { return envelope["error"]["code"].asInt(); }

// Результат вызова инструмента — по-прежнему text-content обёртка.
Json::Value unwrapResult(const Json::Value& envelope) {
    return parseJson(envelope["result"]["content"][0]["text"].asString());
}

// Структурный результат discovery-методов — поля прямо в result.
const Json::Value& structuredResult(const Json::Value& envelope) { return envelope["result"]; }

}  // namespace

TEST(E2eProtocol, ServerDiscoverAnonymous) {
    Json::Value out = parseJson(
        mcpPost(rpcBody("server/discover", Json::Value(Json::nullValue), 0),
                HttpUtil::mcpHeaders("server/discover")).body);
    ASSERT_EQ(out["jsonrpc"].asString(), "2.0");
    ASSERT_FALSE(isError(out)) << out.toStyledString();

    const Json::Value& result = structuredResult(out);
    EXPECT_EQ(result["resultType"].asString(), "complete");
    EXPECT_FALSE(result.isMember("content")) << "discovery не заворачивается в content";
    EXPECT_TRUE(result["supportedVersions"].isArray());
    bool hasCurrent = false;
    for (const auto& v : result["supportedVersions"]) {
        if (v.asString() == "2026-07-28") hasCurrent = true;
    }
    EXPECT_TRUE(hasCurrent);
    EXPECT_TRUE(result["capabilities"]["tools"].isObject());
    EXPECT_EQ(result["_meta"]["io.modelcontextprotocol/serverInfo"]["name"].asString(), "voterpool");
    EXPECT_GT(result["ttlMs"].asInt64(), 0);
    EXPECT_EQ(result["cacheScope"].asString(), "public");
    EXPECT_EQ(result["extensions"]["io.voterpool/domain-events"]["endpoint"].asString(), "/mcp/events");
}

TEST(E2eProtocol, ToolsListStructuredSortedAndComplete) {
    Json::Value out = parseJson(
        mcpPost(rpcBody("tools/list", Json::Value(Json::nullValue), 2),
                HttpUtil::mcpHeaders("tools/list")).body);
    ASSERT_FALSE(isError(out)) << out.toStyledString();

    const Json::Value& result = structuredResult(out);
    EXPECT_EQ(result["resultType"].asString(), "complete");
    EXPECT_FALSE(result.isMember("content"));
    EXPECT_EQ(result["ttlMs"].asInt64(), 300000);
    EXPECT_EQ(result["cacheScope"].asString(), "server");

    const Json::Value& tools = result["tools"];
    ASSERT_TRUE(tools.isArray());
    EXPECT_GE(tools.size(), 15u);
    for (const auto& t : tools) {
        EXPECT_TRUE(t.isMember("description"));
        const Json::Value& schema = t["inputSchema"];
        ASSERT_TRUE(schema.isObject()) << t["name"].asString();
        // JSON Schema draft 2020-12: каждое свойство — объект-схема с type.
        ASSERT_TRUE(schema.isMember("properties")) << t["name"].asString();
        EXPECT_EQ(schema["type"].asString(), "object") << t["name"].asString();
        const Json::Value& props = schema["properties"];
        std::string firstName;
        for (const auto& key : props.getMemberNames()) {
            if (firstName.empty()) firstName = key;
            const Json::Value& prop = props[key];
            ASSERT_TRUE(prop.isObject()) << t["name"].asString() << "." << key
                                         << " must be a schema object, not a literal";
            ASSERT_TRUE(prop.isMember("type")) << t["name"].asString() << "." << key;
            const std::string ty = prop["type"].asString();
            EXPECT_TRUE(ty == "string" || ty == "number" || ty == "integer" || ty == "boolean" ||
                        ty == "array" || ty == "object")
                << t["name"].asString() << "." << key << " has invalid type " << ty;
        }
        // required ⊆ properties; enum decision в cast_vote — все варианты.
        if (schema.isMember("required")) {
            ASSERT_TRUE(schema["required"].isArray()) << t["name"].asString();
            for (const auto& req : schema["required"]) {
                EXPECT_TRUE(props.isMember(req.asString()))
                    << t["name"].asString() << ": required '" << req.asString() << "' not in properties";
            }
        }
        if (t["name"].asString() == "cast_vote") {
            const Json::Value& decision = props["decision"];
            ASSERT_TRUE(decision.isObject());
            ASSERT_TRUE(decision.isMember("enum"));
            ASSERT_TRUE(decision["enum"].isArray());
            EXPECT_EQ(decision["enum"].size(), 3u);
            EXPECT_EQ(decision["enum"][0].asString(), "YES");
            EXPECT_EQ(decision["enum"][1].asString(), "NO");
            EXPECT_EQ(decision["enum"][2].asString(), "ABSTAIN");
        }
    }
    std::string prev;
    for (const auto& t : tools) {
        std::string name = t["name"].asString();
        if (!prev.empty()) EXPECT_LT(prev, name) << "catalog must be lexicographically sorted";
        prev = name;
    }
}

TEST(E2eProtocol, MiddlewareHeaderRules) {
    // Mcp-Method обязан совпадать с методом тела, когда присутствует.
    Json::Value out = parseJson(
        mcpPost(rpcBody("tools/list", Json::Value(Json::nullValue)),
                HttpUtil::mcpHeaders("tools/call", "tools/list")).body);
    EXPECT_TRUE(isError(out));
    EXPECT_EQ(errCode(out), -32600);

    // Отсутствие Mcp-Method/Mcp-Name не ошибка: значения выводятся из тела.
    Json::Value params;
    params["name"] = "get_playbook";
    params["arguments"] = Json::Value(Json::objectValue);
    out = parseJson(mcpPost(rpcBody("tools/call", params), {}).body);
    EXPECT_FALSE(isError(out)) << out.toStyledString();

    // Версия только в _meta (без заголовка версии) принимается.
    Json::Value metaParams(Json::objectValue);
    HttpUtil::setStockClientMeta(metaParams);
    Json::Value body = rpcBody("server/discover", metaParams);
    out = parseJson(mcpPost(body, {{"Mcp-Method", "server/discover"}}).body);
    EXPECT_FALSE(isError(out)) << out.toStyledString();

    // Расхождение заголовка и _meta → -32600 c data.supportedVersions.
    Json::Value mismatchParams;
    mismatchParams["_meta"]["io.modelcontextprotocol/protocolVersion"] = "2025-11-25";
    body = rpcBody("server/discover", mismatchParams);
    out = parseJson(mcpPost(body, HttpUtil::mcpHeaders("server/discover")).body);
    ASSERT_TRUE(isError(out));
    EXPECT_EQ(errCode(out), -32600);
    EXPECT_EQ(out["error"]["data"]["reason"].asString(),
              "MCP-Protocol-Version header does not match _meta protocolVersion");
    bool listedCurrent = false;
    for (const auto& v : out["error"]["data"]["supportedVersions"]) {
        if (v.asString() == "2026-07-28") listedCurrent = true;
    }
    EXPECT_TRUE(listedCurrent);

    // Повторная попытка с взаимно поддерживаемой версией успешна.
    body = rpcBody("server/discover", Json::Value(Json::nullValue));
    out = parseJson(mcpPost(body, HttpUtil::mcpHeaders("server/discover")).body);
    EXPECT_FALSE(isError(out));

    // Метрики ошибок инкрементируются на новом пути отказа.
    HttpResponse metrics = HttpUtil::get(E2eEnv::instance().host(), E2eEnv::instance().port(), "/metrics");
    EXPECT_NE(metrics.body.find("voterpool_rpc_errors_total"), std::string::npos);
}

TEST(E2eProtocol, MissingOrMismatchedHeadersRejected) {
    Json::Value args;
    args["name"] = "Headerless Agent";

    // Несовпадающий Mcp-Name при наличии по-прежнему отклоняется.
    Json::Value mismatch = toolsCall("register_agent", args, {}, "wrong-name");
    EXPECT_TRUE(isError(mismatch));
    EXPECT_EQ(errCode(mismatch), -32600);

    // Полное отсутствие MCP-заголовков теперь валидно: значения из тела.
    Json::Value body = rpcBody("tools/call",
                               [&] {
                                   Json::Value p;
                                   p["name"] = "register_agent";
                                   p["arguments"] = args;
                                   return p;
                               }());
    HttpResponse noHeaders = mcpPost(body, {});
    ASSERT_EQ(noHeaders.status, 200);
    Json::Value parsed = parseJson(noHeaders.body);
    ASSERT_FALSE(parsed.isMember("error")) << parsed.toStyledString();
    Json::Value registered = unwrapResult(parsed);
    EXPECT_TRUE(registered.isMember("agent_id"));
}

TEST(E2eProtocol, AnonymousMethodsAccessibleWithoutAuthorization) {
    // server/discover и tools/list — без Authorization и без Mcp-Name.
    Json::Value out =
        parseJson(mcpPost(rpcBody("server/discover", Json::Value(Json::nullValue)),
                          HttpUtil::mcpHeaders("server/discover")).body);
    EXPECT_FALSE(isError(out));
    out = parseJson(mcpPost(rpcBody("tools/list", Json::Value(Json::nullValue)),
                            HttpUtil::mcpHeaders("tools/list")).body);
    EXPECT_FALSE(isError(out));

    // register_agent и get_playbook — анонимные инструменты.
    Json::Value playbook = toolsCall("get_playbook", Json::Value(Json::objectValue));
    EXPECT_FALSE(isError(playbook));
    Json::Value reg = toolsCall("register_agent", [&] {
        Json::Value a;
        a["name"] = "Anon Probe Agent";
        return a;
    }());
    ASSERT_FALSE(isError(reg));
    EXPECT_TRUE(unwrapResult(reg).isMember("agent_id"));
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

    Json::Value directBody = rpcBody("get_agent", [&] {
        Json::Value p;
        p["agent_id"] = registered["agent_id"];
        return p;
    }());
    auto headers = HttpUtil::mcpHeaders("get_agent", "get_agent");
    headers.push_back({"Authorization", "Bearer " + registered["api_key"].asString()});

    HttpResponse resp = mcpPost(directBody, headers);
    ASSERT_EQ(resp.status, 200);
    Json::Value directOut = parseJson(resp.body);
    ASSERT_FALSE(directOut.isMember("error")) << directOut.toStyledString();
    Json::Value profile = unwrapResult(directOut);
    EXPECT_EQ(profile["agent_id"].asString(), registered["agent_id"].asString());
    EXPECT_EQ(profile["name"].asString(), "Direct Mode Agent");
}

TEST(E2eProtocol, ClientMetaMetricPresence) {
    // Запрос С clientInfo в _meta.
    Json::Value withMeta(Json::objectValue);
    withMeta["_meta"]["io.modelcontextprotocol/clientInfo"]["name"] = "metrics-probe";
    withMeta["_meta"]["io.modelcontextprotocol/clientInfo"]["version"] = "9.9";
    Json::Value listed = parseJson(
        mcpPost(rpcBody("tools/list", withMeta), HttpUtil::mcpHeaders("tools/list")).body);
    ASSERT_FALSE(isError(listed));

    // Запрос БЕЗ clientInfo.
    Json::Value bare = parseJson(
        mcpPost(rpcBody("tools/list", Json::Value(Json::nullValue)),
                HttpUtil::mcpHeaders("tools/list")).body);
    ASSERT_FALSE(isError(bare));

    HttpResponse metrics = HttpUtil::get(E2eEnv::instance().host(), E2eEnv::instance().port(), "/metrics");
    EXPECT_NE(metrics.body.find("voterpool_mcp_client_meta_total{present=\"true\"}"), std::string::npos)
        << "семпл present=true после запроса с clientInfo";
    EXPECT_NE(metrics.body.find("voterpool_mcp_client_meta_total{present=\"false\"}"), std::string::npos)
        << "семпл present=false после запроса без clientInfo";
    // Имя клиента не становится лейблом (дисциплина кардинальности).
    EXPECT_EQ(metrics.body.find("metrics-probe"), std::string::npos);
}

TEST(E2eProtocol, StockClientOnboardingLoop) {
    // Профиль «стоковый клиент»: версия передаётся в params._meta, из
    // обязательных заголовков — только Mcp-Method (+Mcp-Name для тулз).
    auto stockHeaders = [](const std::string& method, const std::string& toolName = "") {
        std::vector<std::pair<std::string, std::string>> headers = {{"Mcp-Method", method}};
        if (!toolName.empty()) headers.push_back({"Mcp-Name", toolName});
        return headers;
    };

    Json::Value discoverParams(Json::objectValue);
    HttpUtil::setStockClientMeta(discoverParams);
    Json::Value body = rpcBody("server/discover", discoverParams, 0);
    Json::Value discover = parseJson(mcpPost(body, stockHeaders("server/discover")).body);
    ASSERT_FALSE(isError(discover));
    ASSERT_EQ(structuredResult(discover)["resultType"].asString(), "complete");

    Json::Value listParams(Json::objectValue);
    HttpUtil::setStockClientMeta(listParams);
    body = rpcBody("tools/list", listParams);
    Json::Value catalog = parseJson(mcpPost(body, stockHeaders("tools/list")).body);
    ASSERT_FALSE(isError(catalog));
    bool sawPlaybookInvite = false;
    for (const auto& t : structuredResult(catalog)["tools"]) {
        if (t["name"].asString() == "get_playbook" && !t["description"].asString().empty()) {
            sawPlaybookInvite = true;
        }
    }
    EXPECT_TRUE(sawPlaybookInvite);

    Json::Value regArgs;
    regArgs["name"] = "Stock Client Agent";
    Json::Value params;
    params["name"] = "register_agent";
    params["arguments"] = regArgs;
    HttpUtil::setStockClientMeta(params);
    Json::Value reg = parseJson(mcpPost(rpcBody("tools/call", params), stockHeaders("tools/call", "register_agent")).body);
    ASSERT_FALSE(isError(reg)) << reg.toStyledString();
    Json::Value agent = unwrapResult(reg);
    ASSERT_TRUE(agent.isMember("api_key"));

    // Рабочий цикл: организация -> предложение -> голос. Авторизация —
    // через _meta-канал токена (docs/05 §1.0), без заголовка Authorization.
    auto callWithAuth = [&](const std::string& tool, const Json::Value& toolArgs) {
        Json::Value p;
        p["name"] = tool;
        p["arguments"] = toolArgs;
        HttpUtil::setStockClientMeta(p);
        p["_meta"]["io.voterpool/auth"]["bearer"] = agent["api_key"].asString();
        return parseJson(mcpPost(rpcBody("tools/call", p), stockHeaders("tools/call", tool)).body);
    };
    Json::Value orgArgs;
    orgArgs["name"] = "Stock Client Org";
    orgArgs["type"] = "OPEN";
    orgArgs["config"] = [&] {
        Json::Value c;
        c["consensus_model"] = "MAJORITY";
        c["quorum_percentage"] = 51;
        c["voting_duration_sec"] = 600;
        c["power_distribution"] = "EQUAL";
        return c;
    }();
    Json::Value org = callWithAuth("create_organization", orgArgs);
    ASSERT_FALSE(isError(org)) << org.toStyledString();
    std::string orgId = unwrapResult(org)["org_id"].asString();

    Json::Value propArgs;
    propArgs["org_id"] = orgId;
    propArgs["title"] = "First decision";
    Json::Value proposal = callWithAuth("create_proposal", propArgs);
    ASSERT_FALSE(isError(proposal)) << proposal.toStyledString();
    std::string pid = unwrapResult(proposal)["proposal_id"].asString();

    Json::Value voteArgs;
    voteArgs["proposal_id"] = pid;
    voteArgs["decision"] = "YES";
    Json::Value voteOut = callWithAuth("cast_vote", voteArgs);
    ASSERT_FALSE(isError(voteOut)) << voteOut.toStyledString();
    EXPECT_EQ(unwrapResult(voteOut)["proposal_status"].asString(), "PASSED");
}

TEST(E2eProtocol, StandardClientHandshakeWithoutCustomHeaders) {
    // Профиль «стандартный клиент SDK»: ни одного кастомного заголовка,
    // версия переговоров — params.protocolVersion.
    const auto& env = E2eEnv::instance();

    // 1. initialize: анонимный, стандартная форма результата.
    HttpResponse initResp =
        HttpUtil::postJson(env.host(), env.port(), "/mcp",
                           rpcBody("initialize",
                                   [&] {
                                       Json::Value p;
                                       p["protocolVersion"] = "2025-06-18";
                                       p["capabilities"] = Json::Value(Json::objectValue);
                                       p["clientInfo"]["name"] = "opencode";
                                       p["clientInfo"]["version"] = "1.0";
                                       return p;
                                   }(),
                                   0),
                           {{"Content-Type", "application/json"},
                            {"Accept", "application/json, text/event-stream"}});
    ASSERT_EQ(initResp.status, 200);
    Json::Value init = parseJson(initResp.body);
    ASSERT_FALSE(init.isMember("error")) << init.toStyledString();
    EXPECT_EQ(init["result"]["protocolVersion"].asString(), "2025-06-18")
        << "поддержанная версия возвращается эхом";
    EXPECT_TRUE(init["result"]["capabilities"]["tools"].isObject());
    EXPECT_EQ(init["result"]["serverInfo"]["name"].asString(), "voterpool");
    EXPECT_FALSE(init["result"].isMember("sessionId")) << "сервер остаётся stateless";

    // 2. notifications/initialized → HTTP 202, пустое тело.
    Json::Value notification;
    notification["jsonrpc"] = "2.0";
    notification["method"] = "notifications/initialized";
    HttpResponse notif = HttpUtil::postJson(env.host(), env.port(), "/mcp", notification,
                                            {{"Content-Type", "application/json"}});
    EXPECT_EQ(notif.status, 202);
    EXPECT_TRUE(notif.body.empty()) << "уведомление не получает JSON-RPC ответа";

    // 3. tools/list без заголовков.
    Json::Value listed = parseJson(
        mcpPost(rpcBody("tools/list", Json::Value(Json::nullValue), 2), {}).body);
    ASSERT_FALSE(isError(listed)) << listed.toStyledString();
    EXPECT_TRUE(structuredResult(listed)["tools"].isArray());

    // 4. tools/call register_agent без заголовков.
    Json::Value reg = parseJson(
        mcpPost(rpcBody("tools/call",
                        [&] {
                            Json::Value p;
                            p["name"] = "register_agent";
                            p["arguments"]["name"] = "Standard SDK Agent";
                            return p;
                        }()),
                {}).body);
    ASSERT_FALSE(isError(reg)) << reg.toStyledString();
    EXPECT_TRUE(unwrapResult(reg).isMember("agent_id"));
}

TEST(E2eProtocol, VersionNegotiationMatrix) {
    // Эхо каноничной версии.
    auto initialize = [](const char* version) {
        return parseJson(
            mcpPost(rpcBody("initialize",
                            [&] {
                                Json::Value p;
                                if (version) p["protocolVersion"] = version;
                                return p;
                            }(),
                            0),
                    {})
                .body);
    };
    Json::Value echoCanonical = initialize("2026-07-28");
    ASSERT_FALSE(isError(echoCanonical));
    EXPECT_EQ(echoCanonical["result"]["protocolVersion"].asString(), "2026-07-28");

    // Fallback для неизвестной версии — без отказа соединения с клиентом.
    Json::Value fallback = initialize("1999-01-01");
    ASSERT_FALSE(isError(fallback)) << fallback.toStyledString();
    EXPECT_EQ(fallback["result"]["protocolVersion"].asString(), "2026-07-28");

    // Не-initialize запрос с неизвестной версией отклоняется со списком.
    Json::Value out = parseJson(
        mcpPost(rpcBody("server/discover", Json::Value(Json::nullValue)),
                {{"MCP-Protocol-Version", "2000-01-01"}}).body);
    ASSERT_TRUE(isError(out));
    EXPECT_EQ(errCode(out), -32600);
    bool sawAll[] = {false, false, false};
    for (const auto& v : out["error"]["data"]["supportedVersions"]) {
        if (v.asString() == "2026-07-28") sawAll[0] = true;
        if (v.asString() == "2025-06-18") sawAll[1] = true;
        if (v.asString() == "2025-03-26") sawAll[2] = true;
    }
    EXPECT_TRUE(sawAll[0] && sawAll[1] && sawAll[2]) << out.toStyledString();

    // Расхождение заголовка и params.protocolVersion → -32600.
    Json::Value conflicting = rpcBody("initialize",
                                      [&] {
                                          Json::Value p;
                                          p["protocolVersion"] = "2025-06-18";
                                          return p;
                                      }());
    out = parseJson(mcpPost(conflicting, {{"MCP-Protocol-Version", "2025-03-26"}}).body);
    ASSERT_TRUE(isError(out));
    EXPECT_EQ(errCode(out), -32600);

    // Запрос вообще без версии (ни заголовка, ни тела, ни _meta) обрабатывается.
    Json::Value noVersion = parseJson(
        mcpPost(rpcBody("server/discover", Json::Value(Json::nullValue)), {}).body);
    EXPECT_FALSE(isError(noVersion)) << noVersion.toStyledString();
}

TEST(E2eProtocol, HttpMethodsOnMcpEndpoint) {
    // GET /mcp → 405 + Allow: POST; SSE-канал возврата отсутствует.
    HttpResponse get = HttpUtil::get(E2eEnv::instance().host(), E2eEnv::instance().port(), "/mcp",
                                     {{"Accept", "text/event-stream"}});
    EXPECT_EQ(get.status, 405);
    EXPECT_NE(get.headers.count("allow") + get.headers.count("Allow"), 0u);

    // DELETE /mcp → 405; сервер stateless, завершать нечего.
    HttpResponse del = HttpUtil::request(E2eEnv::instance().host(), E2eEnv::instance().port(),
                                         "DELETE", "/mcp", {}, "");
    EXPECT_EQ(del.status, 405);
}
