// Генератор и верификатор golden-fixture канонического сценария.
//
// Недетерминированные поля (UUID агентов/орг/предложений, api_key)
// нормализуются стабильными плейсхолдерами AGENT_N / API_KEY_N / ORG_1 /
// PROP_1: сравниваются структура и семантика ответов, а не случайные
// идентификаторы.
//
// Режимы:
//   VOTERPOOL_GOLDEN=write  — записать эталон в VOTERPOOL_GOLDEN_PATH
//   (по умолчанию)          — сравнить с эталоном (deep-equal)
#include "common/Harness.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace {

using namespace voterpool;
using voterpool::testing::Harness;

Json::Value arg(const std::string& k, const Json::Value& v) {
    Json::Value a(Json::objectValue);
    a[k] = v;
    return a;
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to);

void scrub(Json::Value& v, const std::map<std::string, std::string>& map) {
    if (v.isString()) {
        auto it = map.find(v.asString());
        if (it != map.end()) { v = it->second; return; }
        // Составные строки (например, курсор "rev:org_id") — подстрочная замена.
        std::string s = v.asString();
        bool changed = false;
        for (const auto& [raw, ph] : map) {
            if (s.find(raw) != std::string::npos) { s = replaceAll(s, raw, ph); changed = true; }
        }
        if (changed) v = s;
        return;
    }
    if (v.isArray()) {
        for (auto& e : v) scrub(e, map);
        return;
    }
    if (v.isObject()) {
        for (const auto& k : v.getMemberNames()) scrub(v[k], map);
    }
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string normalize(const Json::Value& v) {
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    b["commentStyle"] = "None";
    b["orderKeys"] = true;
    return Json::writeString(b, v);
}

Json::Value runScenario(std::map<std::string, std::string>& idmap) {
    Json::Value log(Json::arrayValue);
    auto step = [&](const std::string& name, const Result<Json::Value>& r) {
        Json::Value entry;
        entry["op"] = name;
        if (r.ok()) {
            entry["ok"] = true;
            entry["value"] = r.value();
        } else {
            entry["ok"] = false;
            entry["code"] = r.error().code;
            entry["message"] = r.error().message;
            entry["data"] = r.error().data;
        }
        log.append(entry);
    };

    auto h = Harness::create();

    // Три агента.
    Json::Value a[3];
    for (int i = 0; i < 3; ++i) {
        auto r = mcp::dispatchToolForTests(*h->app, nullptr, "register_agent",
                                           arg("name", Json::Value("Golden Agent " + std::to_string(i))));
        if (r.ok()) {
            idmap[r.value()["agent_id"].asString()] = "AGENT_" + std::to_string(i);
            idmap[r.value()["api_key"].asString()] = "API_KEY_" + std::to_string(i);
        }
        step("register_agent_" + std::to_string(i), r);
        a[i] = r.value();
    }
    AgentContext ctx[3] = {{a[0]["agent_id"].asString(), "NATIVE", false},
                           {a[1]["agent_id"].asString(), "NATIVE", false},
                           {a[2]["agent_id"].asString(), "NATIVE", false}};

    // Организация OPEN + двое вступают.
    Json::Value orgArgs;
    orgArgs["name"] = "Golden Org";
    orgArgs["type"] = "OPEN";
    orgArgs["short_description"] = "Golden regression org";
    Json::Value cfg;
    cfg["consensus_model"] = "MAJORITY";
    cfg["voting_duration_sec"] = 1000;
    cfg["power_distribution"] = "EQUAL";
    orgArgs["config"] = std::move(cfg);
    auto orgRes = mcp::dispatchToolForTests(*h->app, &ctx[0], "create_organization", orgArgs);
    if (orgRes.ok()) idmap[orgRes.value()["org_id"].asString()] = "ORG_1";
    step("create_organization", orgRes);
    const Json::Value orgId = orgRes.value()["org_id"];

    step("join_agent_1",
         mcp::dispatchToolForTests(*h->app, &ctx[1], "join_organization", arg("org_id", orgId)));
    step("join_agent_2",
         mcp::dispatchToolForTests(*h->app, &ctx[2], "join_organization", arg("org_id", orgId)));

    // Предложение + голоса до консенсуса + дубликат голоса (-32003).
    Json::Value propArgs;
    propArgs["org_id"] = orgId;
    propArgs["title"] = "Golden proposal";
    propArgs["description"] = "Approve golden scenario";
    auto propRes = mcp::dispatchToolForTests(*h->app, &ctx[0], "create_proposal", propArgs);
    if (propRes.ok()) idmap[propRes.value()["proposal_id"].asString()] = "PROP_1";
    step("create_proposal", propRes);
    const Json::Value propId = propRes.value()["proposal_id"];

    Json::Value voteArgs;
    voteArgs["proposal_id"] = propId;
    voteArgs["decision"] = "YES";
    step("cast_vote_1", mcp::dispatchToolForTests(*h->app, &ctx[0], "cast_vote", voteArgs));
    step("cast_vote_2", mcp::dispatchToolForTests(*h->app, &ctx[1], "cast_vote", voteArgs));
    step("cast_vote_duplicate", mcp::dispatchToolForTests(*h->app, &ctx[1], "cast_vote", voteArgs));

    // Поиск: запрос + пагинация курсором (limit 1 → next_cursor).
    Json::Value searchLimited;
    searchLimited["query"] = "golden";
    searchLimited["limit"] = 1;
    step("search_limited",
         mcp::dispatchToolForTests(*h->app, &ctx[2], "search_organizations", searchLimited));
    Json::Value searchAll;
    searchAll["query"] = "golden";
    step("search_all", mcp::dispatchToolForTests(*h->app, &ctx[2], "search_organizations", searchAll));

    // Профиль агента со списком организаций.
    step("get_agent",
         mcp::dispatchToolForTests(*h->app, nullptr, "get_agent", arg("agent_id", a[1]["agent_id"])));

    // SSE-события, доставленные за сценарий.
    Json::Value events(Json::arrayValue);
    for (const auto& e : h->app->hub->eventsForTests()) {
        events.append(e.org_id + ":" + e.event_type);
    }
    Json::Value sse;
    sse["delivered"] = std::move(events);
    Json::Value sseEntry;
    sseEntry["op"] = "sse_events";
    sseEntry["ok"] = true;
    sseEntry["value"] = std::move(sse);
    log.append(std::move(sseEntry));

    return log;
}

}  // namespace

TEST(GoldenRegression, CanonicalScenario) {
    const char* mode = std::getenv("VOTERPOOL_GOLDEN");
    const char* pathEnv = std::getenv("VOTERPOOL_GOLDEN_PATH");
#ifdef VOTERPOOL_GOLDEN_DEFAULT
    const std::string path = pathEnv ? pathEnv : std::string(VOTERPOOL_GOLDEN_DEFAULT);
#else
    const std::string path = pathEnv ? pathEnv : "tests/fixtures/golden_scenario.json";
#endif

    std::map<std::string, std::string> idmap;
    Json::Value actual = runScenario(idmap);
    scrub(actual, idmap);

    if (mode && std::string(mode) == "write") {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream out(path);
        out << normalize(actual);
        GTEST_SKIP() << "fixture written: " << path;
    }
    std::ifstream in(path);
    ASSERT_TRUE(in.good()) << "fixture отсутствует: " << path
                           << " (сгенерируйте VOTERPOOL_GOLDEN=write)";

    Json::Value expected;
    Json::CharReaderBuilder rb;
    std::string errs;
    ASSERT_TRUE(Json::parseFromStream(rb, in, &expected, &errs)) << errs;

    EXPECT_EQ(normalize(actual), normalize(expected))
        << "Golden regression: поведение отличается от эталона до-портов.\n--- expected ---\n"
        << normalize(expected) << "\n--- actual ---\n" << normalize(actual);
}
