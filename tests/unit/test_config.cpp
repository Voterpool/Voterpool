#include "core/Config.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace voterpool;
namespace fs = std::filesystem;

namespace {
std::string writeCfg(const std::string& body) {
    static int n = 0;
    std::string p = fs::temp_directory_path().string() + "/vp_cfg_test_" +
                    std::to_string(getpid()) + "_" + std::to_string(++n) + ".yaml";
    std::ofstream out(p);
    out << body;
    out.close();
    return p;
}

struct EnvGuard {
    std::vector<std::pair<std::string, std::string>> removed;
    explicit EnvGuard(std::initializer_list<const char*> names) {
        for (const char* n : names) {
            const char* v = std::getenv(n);
            if (v) {
                removed.emplace_back(n, v);
                unsetenv(n);
            }
        }
    }
    ~EnvGuard() {
        for (const auto& [k, v] : removed) setenv(k.c_str(), v.c_str(), 1);
    }
};

int runLoad(int argc, char** argv) {
    try {
        AppConfig::load(argc, argv);
        return 0;
    } catch (const ConfigError&) {
        return 1;
    }
}
}  // namespace

TEST(Config, ParsesFullYamlIntoEverySection) {
    std::string p = writeCfg(R"yaml(
server:
  host: "127.0.0.1"
  port: 9090
  threads_num: 4
storage:
  path: "/tmp/vp_cfg_db"
  max_open_files: 128
auth:
  mode: "NATIVE"
sse:
  heartbeat_interval_sec: 7
metrics:
  enabled: false
mcp:
  protocol_version: "2026-07-28"
  tools_list_cache_ttl_ms: 60000
logging:
  level: "debug"
  async: true
  async_queue_size: 4096
rate_limit:
  enabled: false
)yaml");
    EnvGuard guard({"VOTERPOOL_SERVER_PORT", "VOTERPOOL_STORAGE_PATH", "VOTERPOOL_LOGGING_LEVEL"});

    char arg0[] = "voterpool";
    char arg1[] = "--config";
    char* args[] = {arg0, arg1, p.data()};
    AppConfig c = AppConfig::load(3, args);

    EXPECT_EQ(c.server.host, "127.0.0.1");
    EXPECT_EQ(c.server.port, 9090);
    EXPECT_EQ(c.server.threads_num, 4);
    EXPECT_EQ(c.storage.path, "/tmp/vp_cfg_db");
    EXPECT_EQ(c.storage.max_open_files, 128);
    EXPECT_EQ(c.auth.mode, "NATIVE");
    EXPECT_EQ(c.sse.heartbeat_interval_sec, 7);
    EXPECT_FALSE(c.metrics.enabled);
    EXPECT_EQ(c.mcp.tools_list_cache_ttl_ms, 60000u);
    EXPECT_EQ(c.logging.level, "debug");
    EXPECT_EQ(c.logging.async_queue_size, 4096u);
    fs::remove(p);
}

TEST(Config, EnvOverridesFileAndCliBeatsBoth) {
    EnvGuard guard({"VOTERPOOL_SERVER_PORT"});
    std::string p = writeCfg("server:\n  port: 9001\n");

    setenv("VOTERPOOL_SERVER_PORT", "9002", 1);
    char arg0[] = "voterpool";
    char a1[] = "--config";
    std::string cfg = p;
    char* argsEnv[] = {arg0, a1, cfg.data()};
    EXPECT_EQ(AppConfig::load(3, argsEnv).server.port, 9002);

    char portFlag[] = "--port";
    char portVal[] = "9003";
    char* argsCli[] = {arg0, a1, cfg.data(), portFlag, portVal};
    EXPECT_EQ(AppConfig::load(5, argsCli).server.port, 9003);
    unsetenv("VOTERPOOL_SERVER_PORT");
}

TEST(Config, InvalidValuesExitWithConfigError) {
    EnvGuard guard({"VOTERPOOL_SERVER_PORT", "VOTERPOOL_SSE_HEARTBEAT_INTERVAL_SEC"});
    setenv("VOTERPOOL_SERVER_PORT", "-5", 1);
    char arg0[] = "voterpool";
    char* argvOne[] = {arg0};
    EXPECT_EQ(runLoad(1, argvOne), 1);
    unsetenv("VOTERPOOL_SERVER_PORT");

    setenv("VOTERPOOL_SSE_HEARTBEAT_INTERVAL_SEC", "0", 1);
    EXPECT_EQ(runLoad(1, argvOne), 1);
    unsetenv("VOTERPOOL_SSE_HEARTBEAT_INTERVAL_SEC");

    char bad[] = "--unknown-flag";
    char* argsBad[] = {arg0, bad};
    EXPECT_EQ(runLoad(2, argsBad), 1);

    char miss[] = "--config";
    char missVal[] = "/nonexistent/voterpool_cfg.yaml";
    char* argsMiss[] = {arg0, miss, missVal};
    EXPECT_EQ(runLoad(3, argsMiss), 1);
}

TEST(Config, EnvOverrideCoversEveryParameter) {
    EnvGuard guard({
        "VOTERPOOL_SERVER_SSL_CERT_PATH",   "VOTERPOOL_SERVER_SSL_KEY_PATH",
        "VOTERPOOL_SERVER_MAX_REQUEST_BODY_SIZE", "VOTERPOOL_SERVER_REQUEST_TIMEOUT_SEC",
        "VOTERPOOL_STORAGE_WRITE_BUFFER_SIZE", "VOTERPOOL_STORAGE_MAX_WRITE_BUFFER_NUMBER",
        "VOTERPOOL_STORAGE_LOG_LEVEL",      "VOTERPOOL_STORAGE_REBUILD_INDEX_ON_START",
        "VOTERPOOL_AUTH_OIDC_JWKS_URL",     "VOTERPOOL_AUTH_OIDC_ISSUER",
        "VOTERPOOL_AUTH_OIDC_CACHE_TTL_SEC","VOTERPOOL_LOGGING_FORMAT",
        "VOTERPOOL_LOGGING_ASYNC",          "VOTERPOOL_LOGGING_ASYNC_QUEUE_SIZE",
        "VOTERPOOL_RATE_LIMIT_ENABLED",     "VOTERPOOL_RATE_LIMIT_RPS_PER_AGENT",
        "VOTERPOOL_RATE_LIMIT_RPS_PER_ORG",
    });
    std::string p = writeCfg("server:\n  port: 9100\n");

    setenv("VOTERPOOL_SERVER_SSL_CERT_PATH", "/etc/ssl/v.crt", 1);
    setenv("VOTERPOOL_SERVER_SSL_KEY_PATH", "/etc/ssl/v.key", 1);
    setenv("VOTERPOOL_SERVER_MAX_REQUEST_BODY_SIZE", "1048576", 1);
    setenv("VOTERPOOL_SERVER_REQUEST_TIMEOUT_SEC", "77", 1);
    setenv("VOTERPOOL_STORAGE_WRITE_BUFFER_SIZE", "33554432", 1);
    setenv("VOTERPOOL_STORAGE_MAX_WRITE_BUFFER_NUMBER", "7", 1);
    setenv("VOTERPOOL_STORAGE_LOG_LEVEL", "INFO", 1);
    setenv("VOTERPOOL_STORAGE_REBUILD_INDEX_ON_START", "true", 1);
    setenv("VOTERPOOL_AUTH_OIDC_JWKS_URL", "https://idp/jwks.json", 1);
    setenv("VOTERPOOL_AUTH_OIDC_ISSUER", "https://idp", 1);
    setenv("VOTERPOOL_AUTH_OIDC_CACHE_TTL_SEC", "42", 1);
    setenv("VOTERPOOL_LOGGING_FORMAT", "%v", 1);
    setenv("VOTERPOOL_LOGGING_ASYNC", "false", 1);
    setenv("VOTERPOOL_LOGGING_ASYNC_QUEUE_SIZE", "2048", 1);
    setenv("VOTERPOOL_RATE_LIMIT_ENABLED", "true", 1);
    setenv("VOTERPOOL_RATE_LIMIT_RPS_PER_AGENT", "25", 1);
    setenv("VOTERPOOL_RATE_LIMIT_RPS_PER_ORG", "250", 1);

    char arg0[] = "voterpool";
    char a1[] = "--config";
    std::string cfg = p;
    char* args[] = {arg0, a1, cfg.data()};
    AppConfig c = AppConfig::load(3, args);

    EXPECT_EQ(c.server.ssl.cert_path, "/etc/ssl/v.crt");
    EXPECT_EQ(c.server.ssl.key_path, "/etc/ssl/v.key");
    EXPECT_EQ(c.server.max_request_body_size, 1048576u);
    EXPECT_EQ(c.server.request_timeout_sec, 77);
    EXPECT_EQ(c.storage.write_buffer_size, 33554432u);
    EXPECT_EQ(c.storage.max_write_buffer_number, 7);
    EXPECT_EQ(c.storage.log_level, "INFO");
    EXPECT_TRUE(c.storage.rebuild_index_on_start);
    EXPECT_EQ(c.auth.oidc.jwks_url, "https://idp/jwks.json");
    EXPECT_EQ(c.auth.oidc.issuer, "https://idp");
    EXPECT_EQ(c.auth.oidc.cache_ttl_sec, 42);
    EXPECT_EQ(c.logging.format, "%v");
    EXPECT_FALSE(c.logging.async);
    EXPECT_EQ(c.logging.async_queue_size, 2048u);
    EXPECT_TRUE(c.rate_limit.enabled);
    EXPECT_EQ(c.rate_limit.rps_per_agent, 25);
    EXPECT_EQ(c.rate_limit.rps_per_org, 250);
}

TEST(Config, SslEnabledWithoutReadableCertOrKeyIsFatal) {
    AppConfig c;
    c.server.ssl.enabled = true;
    c.server.ssl.cert_path = "/nonexistent/voterpool.crt";
    c.server.ssl.key_path = "/nonexistent/voterpool.key";
    EXPECT_THROW(c.validate(), ConfigError);

    // Читаемые файлы проходят предпроверку путей.
    std::string cert = writeCfg("# cert placeholder\n");
    std::string key = writeCfg("# key placeholder\n");
    c.server.ssl.cert_path = cert;
    c.server.ssl.key_path = key;
    EXPECT_NO_THROW(c.validate());
    fs::remove(cert);
    fs::remove(key);
}

TEST(Config, ClusterModeDefaultsToStandaloneAndRejectsUnknown) {
    AppConfig c;
    EXPECT_EQ(c.cluster.mode, "standalone");
    EXPECT_NO_THROW(c.validate());

    AppConfig bad;
    bad.cluster.mode = "cluster";
    try {
        bad.validate();
        FAIL() << "expected ConfigError for cluster.mode=cluster";
    } catch (const ConfigError& e) {
        EXPECT_NE(std::string(e.what()).find("cluster.mode"), std::string::npos);
    }
}

TEST(Config, McpSupportedVersionsDefault) {
    AppConfig c;
    EXPECT_EQ(c.mcp.supported_versions,
              (std::vector<std::string>{"2026-07-28", "2025-11-25", "2025-06-18", "2025-03-26"}));
    EXPECT_NO_THROW(c.validate());
}

TEST(Config, McpSupportedVersionsParsesYamlAndCsvEnv) {
    EnvGuard guard({"VOTERPOOL_MCP_SUPPORTED_VERSIONS"});
    std::string p = writeCfg(
        "mcp:\n"
        "  protocol_version: \"2026-07-28\"\n"
        "  supported_versions: [\"2026-07-28\", \"2025-06-18\"]\n");
    char arg0[] = "voterpool";
    char a1[] = "--config";
    std::string cfg = p;
    char* args[] = {arg0, a1, cfg.data()};

    AppConfig fromYaml = AppConfig::load(3, args);
    EXPECT_EQ(fromYaml.mcp.supported_versions,
              (std::vector<std::string>{"2026-07-28", "2025-06-18"}));
    EXPECT_NO_THROW(fromYaml.validate());

    setenv("VOTERPOOL_MCP_SUPPORTED_VERSIONS", "2026-07-28, 2025-06-18 , 2025-03-26", 1);
    AppConfig fromEnv = AppConfig::load(3, args);
    EXPECT_EQ(fromEnv.mcp.supported_versions,
              (std::vector<std::string>{"2026-07-28", "2025-06-18", "2025-03-26"}));
    unsetenv("VOTERPOOL_MCP_SUPPORTED_VERSIONS");
    fs::remove(p);
}

TEST(Config, McpSupportedVersionsValidationFailures) {
    AppConfig empty;
    empty.mcp.supported_versions.clear();
    try {
        empty.validate();
        FAIL() << "expected ConfigError for empty supported_versions";
    } catch (const ConfigError& e) {
        EXPECT_NE(std::string(e.what()).find("supported_versions"), std::string::npos);
    }

    AppConfig missing;
    missing.mcp.supported_versions = {"2025-06-18"};
    try {
        missing.validate();
        FAIL() << "expected ConfigError when protocol_version not in supported_versions";
    } catch (const ConfigError& e) {
        EXPECT_NE(std::string(e.what()).find("supported_versions"), std::string::npos);
    }

    AppConfig blank;
    blank.mcp.supported_versions = {"2026-07-28", ""};
    EXPECT_THROW(blank.validate(), ConfigError);
}
