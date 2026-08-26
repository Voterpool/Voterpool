// Строгий контракт аргументов и самодокументируемые схемы (change
// improve-agent-onboarding-contracts, tasks 1.1-1.2).
#include "mcp/McpHandler.h"
#include "mcp/tools/ToolHelpers.h"
#include "mcp/tools/ToolRegistry.h"
#include "tests/common/Harness.h"

#include <gtest/gtest.h>

using namespace voterpool;
using namespace voterpool::testing;

// 1.1: каждое свойство каждой inputSchema каталога имеет непустой description.
TEST(StrictArgs, EverySchemaPropertyHasDescription) {
    for (const auto& def : mcp::catalog()) {
        const Json::Value s = def.schema();
        const Json::Value props = s.get("properties", Json::Value(Json::objectValue));
        for (const auto& key : props.getMemberNames()) {
            ASSERT_TRUE(props[key].isMember("description"))
                << def.name << "." << key << " lacks description";
            EXPECT_FALSE(props[key]["description"].asString().empty())
                << def.name << "." << key << " has empty description";
        }
    }
}

static Json::Value orgArgs(const char* name) {
    Json::Value a;
    a["name"] = name;
    a["type"] = "OPEN";
    Json::Value cfg;
    cfg["consensus_model"] = "MAJORITY";
    cfg["voting_duration_sec"] = 600;
    a["config"] = cfg;
    return a;
}

// 1.2, ветка unknown-key: опечатка даёт -32602 + did-you-mean + allowed_args.
TEST(StrictArgs, UnknownKeyWithTypoGetsHint) {
    auto h = Harness::create();
    AgentContext a = h->registerAgent("typo-agent");
    Json::Value args;
    args["proposal_id"] = generateUuidV4();
    args["decison"] = "YES";  // опечатка
    Json::Value out = h->call("cast_vote", args, &a);
    ASSERT_TRUE(h->isError(out));
    EXPECT_EQ(h->errorCode(out), -32602);
    EXPECT_EQ(out["field"].asString(), "decison");
    EXPECT_EQ(out["hint"].asString(), "did you mean \"decision\"?");
    ASSERT_TRUE(out.isMember("allowed_args"));
}

// 1.2, ветка type-mismatch: tags строкой -> -32602 c expected_type, без молчаливого создания.
TEST(StrictArgs, WrongTypeRejectedInsteadOfSilentIgnore) {
    auto h = Harness::create();
    AgentContext a = h->registerAgent("type-checker");
    Json::Value args = orgArgs("Type Org");
    Json::Value tags(Json::arrayValue);
    tags.append("gov");
    args["tags"] = tags;
    // Контроль: массив тегов проходит.
    Json::Value okOut = h->call("create_organization", args, &a);
    ASSERT_FALSE(h->isError(okOut));

    Json::Value bad = orgArgs("Bad Type Org");
    bad["tags"] = "gov";  // строка вместо array
    Json::Value out = h->call("create_organization", bad, &a);
    ASSERT_TRUE(h->isError(out));
    EXPECT_EQ(h->errorCode(out), -32602);
    EXPECT_EQ(out["field"].asString(), "tags");
    EXPECT_EQ(out["expected_type"].asString(), "array");
}

// 1.2, ветка off-режима флага: strict_arguments=false возвращает прежнее
// поведение с тихим игнорированием неверного типа.
TEST(StrictArgs, FlagDisabledRestoresLegacyTolerantBehavior) {
    AppConfig overrides;
    overrides.mcp.strict_arguments = false;
    auto h = Harness::create(overrides);
    AgentContext a = h->registerAgent("legacy-agent");
    Json::Value bad = orgArgs("Legacy Org");
    bad["tags"] = "gov";
    Json::Value out = h->call("create_organization", bad, &a);
    ASSERT_FALSE(h->isError(out));          // организация создалась
    EXPECT_EQ(out["tags"].size(), 0u);      // ...но поле молча не применилось
}

// Null-аргументы легитимны («ничего не передано») для схем без required.
TEST(StrictArgs, NullArgumentsAreAllowedForOptionalSchemas) {
    auto h = Harness::create();
    AgentContext a = h->registerAgent("null-args");
    Json::Value out = h->call("search_organizations", Json::Value(), &a);
    EXPECT_FALSE(h->isError(out));
}

// Скаляр вместо объекта аргументов отклоняется.
TEST(StrictArgs, NonObjectArgumentsRejected) {
    auto h = Harness::create();
    AgentContext a = h->registerAgent("scalar-args");
    Json::Value scalar("boom");
    Json::Value out = h->call("search_organizations", scalar, &a);
    ASSERT_TRUE(h->isError(out));
    EXPECT_EQ(h->errorCode(out), -32602);
}
