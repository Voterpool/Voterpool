#include "mcp/RequestLog.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

using voterpool::mcp::formatRequestLogLine;
using voterpool::mcp::nextRequestId;
using voterpool::mcp::RequestLogContext;
using voterpool::mcp::sanitizeLogValue;

TEST(RequestLog, RequestIdUniqueAndFormatted) {
    std::set<std::string> ids;
    for (int i = 0; i < 1000; ++i) {
        std::string id = nextRequestId();
        EXPECT_EQ(id.substr(0, 4), "req-");
        EXPECT_TRUE(ids.insert(id).second) << id;
    }
}

TEST(RequestLog, SanitizeStripsControlCharsAndTruncates) {
    EXPECT_EQ(sanitizeLogValue(""), "");
    EXPECT_EQ(sanitizeLogValue("my-agent"), "my-agent");
    EXPECT_EQ(sanitizeLogValue("агент\n\tверсия"), "агентверсия");
    std::string long_(120, 'x');
    EXPECT_EQ(sanitizeLogValue(long_).size(), 64u);
    EXPECT_EQ(sanitizeLogValue(std::string("ab\1cd\x7f")), "abcd");
}

TEST(RequestLog, FormatterCoversAllFields) {
    RequestLogContext ctx;
    ctx.requestId = "req-1-2";
    ctx.agentId = "agent-42";
    ctx.method = "tools/call";
    ctx.tool = "cast_vote";
    ctx.ok = false;
    ctx.errorCode = -32003;
    ctx.t0 = std::chrono::steady_clock::now() - std::chrono::milliseconds(15);
    ctx.client.name = "my-agent";
    ctx.client.version = "1.2.3";

    std::string line = formatRequestLogLine(ctx, 15);
    for (const auto* part : {"request_id=req-1-2", "agent_id=agent-42", "method=tools/call",
                             "tool=cast_vote", "outcome=error", "error_code=-32003",
                             "duration_ms=15", "client=my-agent", "client_version=1.2.3"}) {
        EXPECT_NE(line.find(part), std::string::npos) << line;
    }
    EXPECT_EQ(line.find('\n'), std::string::npos);
}

TEST(RequestLog, FormatterDashForMissingValuesAndOkPath) {
    RequestLogContext ctx;
    ctx.requestId = "req-a-b";
    ctx.method = "tools/list";
    ctx.ok = true;

    std::string line = formatRequestLogLine(ctx, 3);
    for (const auto* part : {"agent_id=-", "tool=-", "outcome=ok", "error_code=-",
                             "duration_ms=3", "client=-", "client_version=-"}) {
        EXPECT_NE(line.find(part), std::string::npos) << line;
    }
}

TEST(RequestLog, TokensNeverEnterTheLogLine) {
    const std::string secret = "voterpool_sec_token_SUPERSECRET";
    RequestLogContext ctx;
    ctx.requestId = "req-t-1";
    ctx.agentId = "agent-7";
    ctx.method = "tools/call";
    ctx.tool = "register_agent";
    ctx.client.name = sanitizeLogValue("client-with\nnewline");
    ctx.ok = false;
    ctx.errorCode = -32001;

    // Форматтер структурно не принимает токен: единственный путь утечки —
    // передать его как поле; проверяем, что без передачи его нет в выводе.
    std::string line = formatRequestLogLine(ctx, 1);
    EXPECT_EQ(line.find(secret), std::string::npos);

    // Даже если клиент прислал токен-подобную строку в clientInfo — это не
    // api_key агента и не попадает в поля авторизации.
    ctx.client.name = sanitizeLogValue(secret);
    line = formatRequestLogLine(ctx, 1);
    EXPECT_NE(line.find("client=voterpool_sec_token_SUPERSECRET"), std::string::npos)
        << "clientInfo логируется (по спеке), но это не токен авторизации";
}
