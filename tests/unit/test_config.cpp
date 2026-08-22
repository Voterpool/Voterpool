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
