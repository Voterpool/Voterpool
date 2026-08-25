#include "core/Config.h"

#include <yaml-cpp/yaml.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace voterpool {
namespace {

std::string envOr(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

long envInt(const char* name, long fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    try {
        return std::stol(v);
    } catch (...) {
        throw ConfigError(std::string("Invalid integer in env ") + name + ": " + v);
    }
}

bool envBool(const char* name, bool fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    std::string s(v);
    return s == "1" || s == "true" || s == "TRUE" || s == "yes" || s == "on";
}

void applyYaml(AppConfig& c, const YAML::Node& root) {
    if (!root.IsMap()) throw ConfigError("Config root must be a mapping");
    auto sec = [&](const char* n) { return root[n] ? root[n] : YAML::Node(YAML::NodeType::Null); };

    if (auto n = sec("server"); n && n.IsMap()) {
        if (n["ssl"] && n["ssl"].IsMap()) {
            if (n["ssl"]["enabled"]) c.server.ssl.enabled = n["ssl"]["enabled"].as<bool>();
            if (n["ssl"]["cert_path"]) c.server.ssl.cert_path = n["ssl"]["cert_path"].as<std::string>();
            if (n["ssl"]["key_path"]) c.server.ssl.key_path = n["ssl"]["key_path"].as<std::string>();
        }
        if (n["host"]) c.server.host = n["host"].as<std::string>();
        if (n["port"]) c.server.port = n["port"].as<int>();
        if (n["threads_num"]) c.server.threads_num = n["threads_num"].as<int>();
        if (n["max_request_body_size"]) c.server.max_request_body_size = n["max_request_body_size"].as<std::uint64_t>();
        if (n["request_timeout_sec"]) c.server.request_timeout_sec = n["request_timeout_sec"].as<int>();
    }
    if (auto n = sec("storage"); n && n.IsMap()) {
        if (n["path"]) c.storage.path = n["path"].as<std::string>();
        if (n["max_open_files"]) c.storage.max_open_files = n["max_open_files"].as<int>();
        if (n["write_buffer_size"]) c.storage.write_buffer_size = n["write_buffer_size"].as<std::uint64_t>();
        if (n["max_write_buffer_number"]) c.storage.max_write_buffer_number = n["max_write_buffer_number"].as<int>();
        if (n["log_level"]) c.storage.log_level = n["log_level"].as<std::string>();
        if (n["rebuild_index_on_start"]) c.storage.rebuild_index_on_start = n["rebuild_index_on_start"].as<bool>();
    }
    if (auto n = sec("auth"); n && n.IsMap()) {
        if (n["mode"]) c.auth.mode = n["mode"].as<std::string>();
        if (n["oidc"] && n["oidc"].IsMap()) {
            if (n["oidc"]["jwks_url"]) c.auth.oidc.jwks_url = n["oidc"]["jwks_url"].as<std::string>();
            if (n["oidc"]["issuer"]) c.auth.oidc.issuer = n["oidc"]["issuer"].as<std::string>();
            if (n["oidc"]["cache_ttl_sec"]) c.auth.oidc.cache_ttl_sec = n["oidc"]["cache_ttl_sec"].as<int>();
        }
    }
    if (auto n = sec("sse"); n && n.IsMap()) {
        if (n["heartbeat_interval_sec"]) c.sse.heartbeat_interval_sec = n["heartbeat_interval_sec"].as<int>();
    }
    if (auto n = sec("metrics"); n && n.IsMap()) {
        if (n["enabled"]) c.metrics.enabled = n["enabled"].as<bool>();
        if (n["path"]) c.metrics.path = n["path"].as<std::string>();
    }
    if (auto n = sec("mcp"); n && n.IsMap()) {
        if (n["protocol_version"]) c.mcp.protocol_version = n["protocol_version"].as<std::string>();
        if (n["tools_list_cache_ttl_ms"]) c.mcp.tools_list_cache_ttl_ms = n["tools_list_cache_ttl_ms"].as<std::uint64_t>();
    }
    if (auto n = sec("cluster"); n && n.IsMap()) {
        if (n["mode"]) c.cluster.mode = n["mode"].as<std::string>();
    }
    if (auto n = sec("logging"); n && n.IsMap()) {
        if (n["level"]) c.logging.level = n["level"].as<std::string>();
        if (n["format"]) c.logging.format = n["format"].as<std::string>();
        if (n["async"]) c.logging.async = n["async"].as<bool>();
        if (n["async_queue_size"]) c.logging.async_queue_size = n["async_queue_size"].as<std::size_t>();
        if (n["log_file"]) c.logging.log_file = n["log_file"].as<std::string>();
    }
    if (auto n = sec("rate_limit"); n && n.IsMap()) {
        if (n["enabled"]) c.rate_limit.enabled = n["enabled"].as<bool>();
        if (n["rps_per_agent"]) c.rate_limit.rps_per_agent = n["rps_per_agent"].as<int>();
        if (n["rps_per_org"]) c.rate_limit.rps_per_org = n["rps_per_org"].as<int>();
    }
}

void applyEnv(AppConfig& c) {
    c.server.ssl.enabled = envBool("VOTERPOOL_SERVER_SSL_ENABLED", c.server.ssl.enabled);
    c.server.ssl.cert_path = envOr("VOTERPOOL_SERVER_SSL_CERT_PATH", c.server.ssl.cert_path);
    c.server.ssl.key_path = envOr("VOTERPOOL_SERVER_SSL_KEY_PATH", c.server.ssl.key_path);
    c.server.host = envOr("VOTERPOOL_SERVER_HOST", c.server.host);
    c.server.port = static_cast<int>(envInt("VOTERPOOL_SERVER_PORT", c.server.port));
    c.server.threads_num = static_cast<int>(envInt("VOTERPOOL_SERVER_THREADS_NUM", c.server.threads_num));
    c.server.max_request_body_size =
        static_cast<std::uint64_t>(envInt("VOTERPOOL_SERVER_MAX_REQUEST_BODY_SIZE",
                                          static_cast<long long>(c.server.max_request_body_size)));
    c.server.request_timeout_sec =
        static_cast<int>(envInt("VOTERPOOL_SERVER_REQUEST_TIMEOUT_SEC", c.server.request_timeout_sec));
    c.storage.path = envOr("VOTERPOOL_STORAGE_PATH", c.storage.path);
    c.storage.max_open_files = static_cast<int>(envInt("VOTERPOOL_STORAGE_MAX_OPEN_FILES", c.storage.max_open_files));
    c.storage.write_buffer_size =
        static_cast<std::uint64_t>(envInt("VOTERPOOL_STORAGE_WRITE_BUFFER_SIZE",
                                          static_cast<long long>(c.storage.write_buffer_size)));
    c.storage.max_write_buffer_number =
        static_cast<int>(envInt("VOTERPOOL_STORAGE_MAX_WRITE_BUFFER_NUMBER", c.storage.max_write_buffer_number));
    c.storage.log_level = envOr("VOTERPOOL_STORAGE_LOG_LEVEL", c.storage.log_level);
    c.storage.rebuild_index_on_start =
        envBool("VOTERPOOL_STORAGE_REBUILD_INDEX_ON_START", c.storage.rebuild_index_on_start);
    c.auth.mode = envOr("VOTERPOOL_AUTH_MODE", c.auth.mode);
    c.auth.oidc.jwks_url = envOr("VOTERPOOL_AUTH_OIDC_JWKS_URL", c.auth.oidc.jwks_url);
    c.auth.oidc.issuer = envOr("VOTERPOOL_AUTH_OIDC_ISSUER", c.auth.oidc.issuer);
    c.auth.oidc.cache_ttl_sec =
        static_cast<int>(envInt("VOTERPOOL_AUTH_OIDC_CACHE_TTL_SEC", c.auth.oidc.cache_ttl_sec));
    c.sse.heartbeat_interval_sec = static_cast<int>(envInt("VOTERPOOL_SSE_HEARTBEAT_INTERVAL_SEC", c.sse.heartbeat_interval_sec));
    c.metrics.enabled = envBool("VOTERPOOL_METRICS_ENABLED", c.metrics.enabled);
    c.metrics.path = envOr("VOTERPOOL_METRICS_PATH", c.metrics.path);
    c.mcp.protocol_version = envOr("VOTERPOOL_MCP_PROTOCOL_VERSION", c.mcp.protocol_version);
    c.mcp.tools_list_cache_ttl_ms = static_cast<std::uint64_t>(envInt("VOTERPOOL_MCP_TOOLS_LIST_CACHE_TTL_MS", static_cast<long long>(c.mcp.tools_list_cache_ttl_ms)));
    c.logging.level = envOr("VOTERPOOL_LOGGING_LEVEL", c.logging.level);
    c.logging.format = envOr("VOTERPOOL_LOGGING_FORMAT", c.logging.format);
    c.logging.async = envBool("VOTERPOOL_LOGGING_ASYNC", c.logging.async);
    c.logging.async_queue_size = static_cast<std::size_t>(
        envInt("VOTERPOOL_LOGGING_ASYNC_QUEUE_SIZE", static_cast<long long>(c.logging.async_queue_size)));
    c.logging.log_file = envOr("VOTERPOOL_LOGGING_LOG_FILE", c.logging.log_file);
    c.rate_limit.enabled = envBool("VOTERPOOL_RATE_LIMIT_ENABLED", c.rate_limit.enabled);
    c.rate_limit.rps_per_agent =
        static_cast<int>(envInt("VOTERPOOL_RATE_LIMIT_RPS_PER_AGENT", c.rate_limit.rps_per_agent));
    c.rate_limit.rps_per_org =
        static_cast<int>(envInt("VOTERPOOL_RATE_LIMIT_RPS_PER_ORG", c.rate_limit.rps_per_org));
}

struct CliOptions {
    std::string configPath = "./config.yaml";
    bool havePort = false;
    int port = 0;
    bool haveDbPath = false;
    std::string dbPath;
    bool haveLogLevel = false;
    std::string logLevel;
    bool daemon = false;
};

CliOptions parseCli(int argc, char** argv) {
    CliOptions o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw ConfigError("Missing value for flag " + a);
            return argv[++i];
        };
        if (a == "--config") o.configPath = next();
        else if (a == "--port") { o.port = std::stoi(next()); o.havePort = true; }
        else if (a == "--db-path") { o.dbPath = next(); o.haveDbPath = true; }
        else if (a == "--log-level") { o.logLevel = next(); o.haveLogLevel = true; }
        else if (a == "--daemon") o.daemon = true;
        else if (a == "checkpoint") { /* subcommand handled by main */ }
        else throw ConfigError("Unknown CLI flag: " + a);
    }
    return o;
}

}  // namespace

AppConfig AppConfig::load(int argc, char** argv) {
    CliOptions cli = parseCli(argc, argv);

    AppConfig c;
    if (std::filesystem::exists(cli.configPath)) {
        std::ifstream fin(cli.configPath);
        if (!fin) throw ConfigError("Cannot open config file: " + cli.configPath);
        try {
            applyYaml(c, YAML::Load(fin));
        } catch (const YAML::Exception& e) {
            throw ConfigError("Invalid YAML in " + cli.configPath + ": " + e.what());
        }
    } else {
        throw ConfigError("Config file not found: " + cli.configPath +
                          " (pass --config <path>)");
    }

    applyEnv(c);

    if (cli.havePort) c.server.port = cli.port;
    if (cli.haveDbPath) c.storage.path = cli.dbPath;
    if (cli.haveLogLevel) c.logging.level = cli.logLevel;

    c.validate();
    if (cli.daemon) {
        pid_t pid = fork();
        if (pid < 0) throw ConfigError("fork() failed for --daemon");
        if (pid > 0) _exit(0);
        setsid();
    }
    return c;
}

void AppConfig::validate() const {
    if (server.port < 0 || server.port > 65535)
        throw ConfigError("server.port must be in [0; 65535], got " + std::to_string(server.port));
    if (server.threads_num < 0)
        throw ConfigError("server.threads_num must be >= 0");
    if (server.request_timeout_sec <= 0)
        throw ConfigError("server.request_timeout_sec must be > 0");
    if (server.max_request_body_size == 0)
        throw ConfigError("server.max_request_body_size must be > 0");
    if (server.ssl.enabled) {
        for (const char* what : {"cert_path", "key_path"}) {
            const std::string& path = std::string(what) == "cert_path" ? server.ssl.cert_path
                                                                       : server.ssl.key_path;
            std::error_code ec;
            if (path.empty() || !std::filesystem::exists(path, ec) || !std::filesystem::is_regular_file(path, ec) ||
                access(path.c_str(), R_OK) != 0) {
                throw ConfigError("server.ssl." + std::string(what) + " is not a readable file: '" + path +
                                  "'");
            }
        }
    }
    if (storage.write_buffer_size == 0)
        throw ConfigError("storage.write_buffer_size must be > 0");
    if (storage.max_write_buffer_number <= 0)
        throw ConfigError("storage.max_write_buffer_number must be > 0");
    if (auth.mode != "NATIVE" && auth.mode != "OIDC")
        throw ConfigError("auth.mode must be NATIVE or OIDC");
    if (auth.mode == "OIDC")
        throw ConfigError("OIDC auth mode is not available in the On-Premises (Self-Hosted) edition");
    if (cluster.mode != "standalone")
        throw ConfigError("cluster.mode must be \"standalone\" at this stage; got \"" +
                          cluster.mode + "\"");
    if (sse.heartbeat_interval_sec <= 0)
        throw ConfigError("sse.heartbeat_interval_sec must be > 0");
    if (mcp.protocol_version.empty())
        throw ConfigError("mcp.protocol_version must not be empty");
    if (mcp.tools_list_cache_ttl_ms == 0)
        throw ConfigError("mcp.tools_list_cache_ttl_ms must be > 0");
    if (logging.async_queue_size == 0 || (logging.async_queue_size & (logging.async_queue_size - 1)) != 0)
        throw ConfigError("logging.async_queue_size must be a power of two");
    if (logging.level.empty())
        throw ConfigError("logging.level must not be empty");
}

}  // namespace voterpool
