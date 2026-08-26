// Плейбук v2: точность защищается тестом — все JSON-шаблоны текста
// проверяются против живого каталога инструментов (task 4.1).
#include "mcp/McpHandler.h"
#include "mcp/tools/ToolHelpers.h"
#include "mcp/tools/ToolRegistry.h"
#include "tests/common/Harness.h"

#include <gtest/gtest.h>

#include <json/json.h>
#include <regex>
#include <set>
#include <string>
#include <vector>

using namespace voterpool;
using namespace voterpool::testing;

namespace {

std::string playbookText() {
    auto h = Harness::create();
    Json::Value out = h->call("get_playbook", Json::Value(Json::objectValue), nullptr);
    EXPECT_FALSE(h->isError(out));
    return out.isMember("playbook") ? out["playbook"].asString() : std::string();
}

std::vector<std::string> extractToolNames(const std::string& text) {
    // {"name":"<tool>","arguments":...}
    // Сырые кавычки внутри строковой литералы regex — экранируем без R"(...)"
    // вложенных конфликтов: используем отдельные фрагменты.
    static const std::regex re("\\{\\s*\"name\"\\s*:\\s*\"([a-z_]+)\"");
    std::vector<std::string> names;
    auto begin = std::sregex_iterator(text.begin(), text.end(), re);
    for (auto it = begin; it != std::sregex_iterator(); ++it) names.push_back((*it)[1]);
    return names;
}

// Достаёт JSON объекты шаблонов: от "{" перед "name": до парной скобки.
std::vector<std::pair<std::string, Json::Value>> parseTemplates(const std::string& text) {
    std::vector<std::pair<std::string, Json::Value>> out;
    auto names = extractToolNames(text);
    size_t searchFrom = 0;
    for (const auto& tool : names) {
        const std::string key = "\"name\":\"" + tool + "\"";
        // Текст может содержать пробелы: {"name":"whoami","arguments"...}
        const std::string alt = "\"name\":\"" + tool + "\"";
        size_t namePos = text.find(alt, searchFrom);
        if (namePos == std::string::npos) continue;
        // Начало объекта: ближайший '{' слева.
        size_t start = text.rfind('{', namePos);
        if (start == std::string::npos) continue;
        int depth = 0;
        bool inStr = false;
        char prev = 0;
        size_t end = start;
        for (size_t i = start; i < text.size(); ++i) {
            char c = text[i];
            if (inStr) {
                if (c == '"' && prev != '\\') inStr = false;
            } else if (c == '"') {
                inStr = true;
            } else if (c == '{') {
                ++depth;
            } else if (c == '}') {
                --depth;
                if (depth == 0) { end = i; break; }
            }
            prev = c;
        }
        std::string jsonText = text.substr(start, end - start + 1);
        Json::Value parsed;
        Json::CharReaderBuilder rb;
        std::string errs;
        std::istringstream in(jsonText);
        if (Json::parseFromStream(rb, in, &parsed, &errs)) {
            out.emplace_back(tool, parsed);
            searchFrom = end + 1;
        }
    }
    return out;
}

const Json::Value schemaPropertiesFor(const std::string& tool) {
    for (const auto& d : mcp::catalog()) {
        if (tool == d.name) return d.schema().get("properties", Json::Value(Json::objectValue));
    }
    return Json::Value(Json::objectValue);
}

}  // namespace

TEST(PlaybookJson, EveryTemplateTargetsExistingToolWithValidArgs) {
    const std::string text = playbookText();
    ASSERT_FALSE(text.empty());
    auto templates = parseTemplates(text);
    ASSERT_GE(templates.size(), 12u) << "playbook must carry full JSON templates";

    std::set<std::string> covered;
    for (const auto& [tool, call] : templates) {
        SCOPED_TRACE(tool);
        bool exists = false;
        for (const auto& d : mcp::catalog()) exists |= (d.name == tool);
        ASSERT_TRUE(exists) << "playbook references unknown tool: " << tool;

        const Json::Value props = schemaPropertiesFor(tool);
        const Json::Value args = call.get("arguments", Json::Value(Json::objectValue));
        for (const auto& key : args.getMemberNames()) {
            // Вложенные ключи не проверяем здесь: их точность защищают
            // строгие парсеры и e2e-матрица.
            ASSERT_TRUE(props.isMember(key))
                << "template argument not in inputSchema: " << tool << "." << key;
        }
        covered.insert(tool);
    }

    // Ключевые шаги онбординга обязаны присутствовать шаблонами.
    for (const char* required :
         {"whoami", "register_agent", "update_agent", "search_organizations",
          "join_organization", "list_pending_members", "get_proposals", "cast_vote",
          "create_proposal", "wait_proposal_close", "get_proposal", "get_organization"}) {
        EXPECT_TRUE(covered.count(required)) << "playbook lacks template for " << required;
    }
}

TEST(PlaybookNormative, MustAndMustNotSectionsPresentWithEnumCaseRules) {
    const std::string text = playbookText();
    EXPECT_NE(text.find("MUST NOT"), std::string::npos);
    EXPECT_NE(text.find("MUST"), std::string::npos);
    EXPECT_NE(text.find("case-sensitive"), std::string::npos);
    // Энумы перечислены в UPPER_CASE виде.
    for (const char* e : {"MAJORITY", "CONSENT", "QUORUM_PERCENTAGE", "EQUAL", "SHARES",
                          "APPROVE_MEMBER", "UPDATE_ORG_INFO", "ABSTAIN"}) {
        EXPECT_NE(text.find(e), std::string::npos) << e << " missing from enum list";
    }
    // Запрещённые действия присутствуют с альтернативами.
    EXPECT_NE(text.find("create_proposal - they belong to create_organization"),
              std::string::npos);
    EXPECT_NE(text.find("-32001"), std::string::npos);
    EXPECT_NE(text.find("identity_warning"), std::string::npos);
}
