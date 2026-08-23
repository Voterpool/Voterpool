#pragma once

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <unistd.h>

namespace voterpool::mcp {

struct ClientMeta {
    std::string name;
    std::string version;
    bool present = false;
};

// request_id: уникален в пределах процесса (pid + монотонный счётчик).
inline std::string nextRequestId() {
    static std::atomic<std::uint64_t> counter{0};
    char buf[48];
    std::snprintf(buf, sizeof(buf), "req-%lx-%llx",
                  static_cast<unsigned long>(::getpid()),
                  static_cast<unsigned long long>(counter.fetch_add(1, std::memory_order_relaxed)));
    return buf;
}

// Одна строка без управляющих символов, обрезанная до безопасной длины.
inline std::string sanitizeLogValue(const std::string& value) {
    constexpr size_t kMaxLen = 64;
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7f) continue;
        out.push_back(c);
        if (out.size() >= kMaxLen) break;
    }
    return out;
}

struct RequestLogContext {
    std::string requestId;
    std::string agentId;       // "-" если не авторизован
    std::string method;
    std::string tool;
    bool ok = true;
    int errorCode = 0;         // значение при ok != true
    std::chrono::steady_clock::time_point t0{std::chrono::steady_clock::now()};
    ClientMeta client;
};

inline std::string formatRequestLogLine(const RequestLogContext& ctx, long durationMs) {
    std::string line = "request_id=" + ctx.requestId;
    line += " agent_id=" + (ctx.agentId.empty() ? "-" : sanitizeLogValue(ctx.agentId));
    line += " method=" + (ctx.method.empty() ? "-" : sanitizeLogValue(ctx.method));
    line += " tool=" + (ctx.tool.empty() ? "-" : sanitizeLogValue(ctx.tool));
    line += ctx.ok ? " outcome=ok" : " outcome=error";
    line += " error_code=" + (ctx.ok ? std::string("-") : std::to_string(ctx.errorCode));
    line += " duration_ms=" + std::to_string(durationMs);
    line += " client=" + (ctx.client.name.empty() ? std::string("-") : sanitizeLogValue(ctx.client.name));
    line += " client_version=" +
            (ctx.client.version.empty() ? std::string("-") : sanitizeLogValue(ctx.client.version));
    return line;
}

inline void emitRequestLog(const RequestLogContext& ctx) {
    const long durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - ctx.t0)
                                .count();
    spdlog::info("{}", formatRequestLogLine(ctx, durationMs));
}

}  // namespace voterpool::mcp
