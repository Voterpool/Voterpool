#include "mcp/tools/ToolHelpers.h"

#include <set>

namespace voterpool::mcp {

ToolDef defSearchOrganizations() {
    return ToolDef{
        "search_organizations",
        "Search and feed of ACTIVE organizations: by name substring, tags (AND), category, with cursor pagination",
        [] {
            return schemaObject({{"query", schemaString(
                "Case-insensitive substring of the organization name, e.g. \"governance lab\"")},
                                 {"tags", schemaArrayOf("string",
                     "Keywords AND-combined; matching is case-sensitive-normalized to lowercase at creation")},
                                 {"category", schemaString("Category keyword filter")},
                                 {"type", schemaEnumOf({"OPEN", "CLOSED"},
                     "Case-sensitive filter by join policy")},
                                 {"cursor", schemaString("Opaque next_cursor from the previous page; do not construct manually")},
                                 {"limit", schemaInteger("Page size in [1;100], default 50")}},
                                {});
        },
        [](ToolContext& tc, const Json::Value& args) -> Result<Json::Value> {
            std::int64_t limit = 50;
            if (args.isMember("limit")) {
                if (!args["limit"].isIntegral()) return RpcError::invalidParams("limit must be an integer");
                limit = args["limit"].asInt64();
                if (limit < 1 || limit > 100) return RpcError::invalidParams("limit must be in [1;100]");
            }
            std::string query, category, typeFilter, cursor;
            std::vector<std::string> tags;
            if (args.isMember("query")) {
                auto q = argString(args, "query", false, true);
                if (!q.ok()) return q.error();
                query = Keys::nameLower(q.value());
            }
            if (args.isMember("category")) {
                auto c = argString(args, "category", false, true);
                if (!c.ok()) return c.error();
                category = Keys::nameLower(c.value());
            }
            if (args.isMember("type")) {
                auto t = argString(args, "type", false, true);
                if (!t.ok()) return t.error();
                typeFilter = t.value();
                if (typeFilter != "OPEN" && typeFilter != "CLOSED")
                    return RpcError::invalidParams("type must be OPEN or CLOSED");
            }
            if (args.isMember("cursor") && args["cursor"].isString()) cursor = args["cursor"].asString();
            if (args.isMember("tags")) {
                if (!args["tags"].isArray()) return RpcError::invalidParams("tags must be an array");
                for (const auto& t : args["tags"]) {
                    if (!t.isString()) return RpcError::invalidParams("tags items must be strings");
                    tags.push_back(Keys::tagLower(t.asString()));
                }
            }

            std::set<std::string> candidates;
            bool haveCandidateSet = false;
            if (!query.empty()) {
                candidates = tc.app.directory->matchName(query);
                haveCandidateSet = true;
            }
            for (const auto& t : tags) {
                auto s = tc.app.directory->scanTag(t);
                if (haveCandidateSet) {
                    std::set<std::string> inter;
                    for (const auto& id : s) {
                        if (candidates.count(id)) inter.insert(id);
                    }
                    candidates = std::move(inter);
                } else {
                    candidates = std::move(s);
                    haveCandidateSet = true;
                }
            }
            if (!category.empty()) {
                auto s = tc.app.directory->scanCategory(category);
                if (haveCandidateSet) {
                    std::set<std::string> inter;
                    for (const auto& id : s) {
                        if (candidates.count(id)) inter.insert(id);
                    }
                    candidates = std::move(inter);
                } else {
                    candidates = std::move(s);
                    haveCandidateSet = true;
                }
            }

            std::int64_t cursorRev = -1;
            std::string cursorOrg;
            if (!cursor.empty()) {
                size_t colon = cursor.find(':');
                if (colon != std::string::npos) {
                    try {
                        cursorRev = std::stoll(cursor.substr(0, colon));
                        cursorOrg = cursor.substr(colon + 1);
                    } catch (...) {
                        return RpcError::invalidParams("Invalid cursor");
                    }
                } else {
                    return RpcError::invalidParams("Invalid cursor");
                }
            }

            Json::Value items(Json::arrayValue);
            std::string nextCursor;
            for (const auto& orgId : tc.app.directory->scanFeedActive()) {
                if (cursorRev >= 0) {
                    auto orgOpt = tc.app.orgs->get(orgId);
                    std::int64_t rev = orgOpt ? Keys::reverseTs(orgOpt->created_at) : -1;
                    if (rev < cursorRev || (rev == cursorRev && orgId <= cursorOrg)) continue;
                }
                if (haveCandidateSet && !candidates.count(orgId)) continue;
                auto orgOpt = tc.app.orgs->get(orgId);
                if (!orgOpt || orgOpt->status != OrgStatus::ACTIVE) continue;
                if (!typeFilter.empty() && toString(orgOpt->type) != typeFilter) continue;

                Json::Value item;
                item["org_id"] = orgOpt->org_id;
                item["name"] = orgOpt->name;
                item["short_description"] = orgOpt->short_description;
                item["tags"] = Codec::tagsToJson(orgOpt->tags);
                item["type"] = toString(orgOpt->type);
                item["active_members"] = tc.app.orgs->countActiveMembers(orgId);
                item["max_agents"] = static_cast<Json::Int64>(orgOpt->max_agents);
                item["consensus_model"] = toString(orgOpt->config.consensus_model);
                item["created_at"] = static_cast<Json::Int64>(orgOpt->created_at);
                items.append(item);

                if (static_cast<std::int64_t>(items.size()) >= limit) {
                    std::int64_t revLast = Keys::reverseTs(orgOpt->created_at);
                    nextCursor = std::to_string(revLast) + ":" + orgId;
                    break;
                }
            }

            Json::Value out;
            out["items"] = std::move(items);
            out["next_cursor"] = nextCursor;
            return out;
        }};
}

}  // namespace voterpool::mcp
