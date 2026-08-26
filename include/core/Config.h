#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace voterpool {

struct SslConfig {
    bool enabled = false;
    std::string cert_path;
    std::string key_path;
};

struct ServerConfig {
    SslConfig ssl;
    std::string host = "0.0.0.0";
    int port = 8080;
    int threads_num = 0;
    std::uint64_t max_request_body_size = 10 * 1024 * 1024;
    int request_timeout_sec = 30;
};

struct StorageConfig {
    std::string path = "./data/voterpool_db";
    int max_open_files = -1;
    std::uint64_t write_buffer_size = 64ull * 1024 * 1024;
    int max_write_buffer_number = 3;
    std::string log_level = "WARN";
    bool rebuild_index_on_start = false;
};

struct OidcConfig {
    std::string jwks_url;
    std::string issuer;
    int cache_ttl_sec = 300;
};

struct AuthConfig {
    std::string mode = "NATIVE";
    OidcConfig oidc;
};

struct SseConfig {
    int heartbeat_interval_sec = 15;
};

struct MetricsConfig {
    bool enabled = true;
    std::string path = "/metrics";
};

struct McpConfig {
    std::string protocol_version = "2026-07-28";
    std::vector<std::string> supported_versions = {"2026-07-28", "2025-11-25", "2025-06-18",
                                                   "2025-03-26"};
    std::uint64_t tools_list_cache_ttl_ms = 300000;
};

struct LoggingConfig {
    std::string level = "info";
    std::string format = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v";
    bool async = true;
    std::size_t async_queue_size = 8192;
    std::string log_file;
};

struct ClusterConfig {
    std::string mode = "standalone";
};

struct RateLimitConfig {
    bool enabled = false;
    int rps_per_agent = 50;
    int rps_per_org = 500;
};

struct AppConfig {
    ServerConfig server;
    StorageConfig storage;
    AuthConfig auth;
    SseConfig sse;
    MetricsConfig metrics;
    McpConfig mcp;
    LoggingConfig logging;
    RateLimitConfig rate_limit;
    ClusterConfig cluster;

    static AppConfig load(int argc, char** argv);
    void validate() const;
};

class ConfigError : public std::runtime_error {
public:
    explicit ConfigError(const std::string& msg) : std::runtime_error(msg) {}
};

}  // namespace voterpool
