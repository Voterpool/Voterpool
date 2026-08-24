#include "core/Metrics.h"

#include <gtest/gtest.h>

#include <string>

using namespace voterpool;

namespace {

std::string exposeAfterDefaults() {
    MetricsRegistry::instance().resetForTests();
    MetricsRegistry::instance().registerDefaults();
    return MetricsRegistry::instance().expose();
}

std::size_t posOf(const std::string& text, const std::string& needle) {
    return text.find(needle);
}

}  // namespace

TEST(MetricsExposition, HelpPrintedBeforeTypeForEveryFamily) {
    const std::string out = exposeAfterDefaults();

    static const char* kFamilies[] = {
        "voterpool_actions_applied_total",
        "voterpool_agents_total",
        "voterpool_consensus_early_exit_total",
        "voterpool_db_healthy",
        "voterpool_mcp_requests_total",
        "voterpool_orgs_active",
        "voterpool_proposals_closed_total",
        "voterpool_sse_connections",
        "voterpool_ttl_scans_total",
        "voterpool_votes_cast_total",
    };
    for (const char* family : kFamilies) {
        const std::string help = std::string("# HELP ") + family + " ";
        const std::string type = std::string("# TYPE ") + family + " ";
        ASSERT_NE(posOf(out, help), std::string::npos) << "missing HELP for " << family;
        ASSERT_NE(posOf(out, type), std::string::npos) << "missing TYPE for " << family;
        EXPECT_LT(posOf(out, help), posOf(out, type)) << "HELP must precede TYPE for " << family;
    }
}

TEST(MetricsExposition, HelpTextIsHumanReadableNotTheName) {
    const std::string out = exposeAfterDefaults();

    EXPECT_NE(out.find("# HELP voterpool_consensus_early_exit_total Early-Exit optimization triggers during voting\n"),
              std::string::npos);
    EXPECT_NE(out.find("# HELP voterpool_agents_total Registered agents\n"), std::string::npos);
    EXPECT_EQ(out.find("# HELP voterpool_agents_total voterpool_agents_total\n"), std::string::npos);
}

TEST(MetricsExposition, AgentsTotalExposedAsGauge) {
    const std::string out = exposeAfterDefaults();
    EXPECT_NE(out.find("# TYPE voterpool_agents_total gauge\n"), std::string::npos);
}

TEST(MetricsExposition, HistogramsGetHelpBeforeType) {
    MetricsRegistry::instance().resetForTests();
    MetricsRegistry::instance().registerDefaults();
    MetricsRegistry::instance().observe("voterpool_http_request_duration_seconds", 0.001);

    const std::string out = MetricsRegistry::instance().expose();
    const auto help = posOf(out, "# HELP voterpool_http_request_duration_seconds POST /mcp request latency\n");
    const auto type = posOf(out, "# TYPE voterpool_http_request_duration_seconds histogram\n");
    ASSERT_NE(help, std::string::npos);
    ASSERT_NE(type, std::string::npos);
    EXPECT_LT(help, type);
}
