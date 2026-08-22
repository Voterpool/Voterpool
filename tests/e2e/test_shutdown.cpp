#include "tests/common/HttpUtil.h"
#include "tests/common/SseClient.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <csignal>
#include <cstdlib>
#include <netinet/in.h>
#include <sys/socket.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

#ifndef VOTERPOOL_BIN
#define VOTERPOOL_BIN "./voterpool"
#endif

using namespace voterpool;
using namespace voterpool::testing;

namespace {

std::string tempDir() {
    return std::filesystem::temp_directory_path().string() + "/voterpool_shutdown_" +
           std::to_string(getpid()) + "_" + std::to_string(std::random_device{}());
}

Json::Value parseJson(const std::string& body) {
    Json::Value out;
    if (body.empty()) return out;
    Json::CharReaderBuilder b;
    std::string errs;
    std::istringstream iss(body);
    Json::parseFromStream(b, iss, &out, &errs);
    return out;
}

int freePort() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    socklen_t len = sizeof(addr);
    getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    int port = ntohs(addr.sin_port);
    close(fd);
    return port;
}

}  // namespace

TEST(E2eShutdown, SigtermDrainsSseAndExitsZero) {
    std::string dbDir = tempDir();
    int port = freePort();
    std::string configPath = dbDir + ".yaml";
    {
        std::ofstream out(configPath);
        out << "server:\n  host: \"127.0.0.1\"\n  port: " << port << "\n";
        out << "storage:\n  path: \"" << dbDir << "\"\n";
        out << "sse:\n  heartbeat_interval_sec: 1\n";
    }

    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        execl(VOTERPOOL_BIN, VOTERPOOL_BIN, "--config", configPath.c_str(), (char*)nullptr);
        _exit(127);
    }

    ASSERT_TRUE(HttpUtil::waitForHttp("127.0.0.1", port, "/health", 20000)) << "server did not start";

    Json::Value regBody;
    regBody["jsonrpc"] = "2.0";
    regBody["id"] = 1;
    regBody["method"] = "tools/call";
    regBody["params"]["name"] = "register_agent";
    regBody["params"]["arguments"]["name"] = "shutdown-probe";
    HttpResponse reg = HttpUtil::postJson("127.0.0.1", port, "/mcp", regBody,
                                          {{"Content-Type", "application/json"},
                                           {"MCP-Protocol-Version", "2026-07-28"},
                                           {"Mcp-Method", "tools/call"},
                                           {"Mcp-Name", "register_agent"}});
    ASSERT_EQ(reg.status, 200);
    Json::Value parsed;
    {
        Json::CharReaderBuilder b;
        std::string errs;
        std::istringstream iss(reg.body);
        Json::parseFromStream(b, iss, &parsed, &errs);
    }
    std::string token = parseJson(parsed["result"]["content"][0]["text"].asString())["api_key"].asString();
    ASSERT_FALSE(token.empty());

    SseClient sse("127.0.0.1", port, token);
    ASSERT_TRUE(sse.connected());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ASSERT_EQ(kill(pid, SIGTERM), 0);

    bool gotShutdownFrame = false;
    for (int i = 0; i < 10 && !gotShutdownFrame; ++i) {
        auto ev = sse.nextEvent(1000);
        if (ev.has_value() && ev->event == "server_shutdown") gotShutdownFrame = true;
    }
    EXPECT_TRUE(gotShutdownFrame) << "expected server_shutdown frame during drain";

    int status = 0;
    int waited = 0;
    while (waitpid(pid, &status, WNOHANG) == 0 && waited < 15000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waited += 100;
    }
    ASSERT_NE(waitpid(pid, &status, WNOHANG), 0) << "process did not exit after SIGTERM";
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);

    HttpResponse after = HttpUtil::get("127.0.0.1", port, "/health", {}, 300);
    EXPECT_EQ(after.status, 0);

    std::error_code ec;
    std::filesystem::remove_all(dbDir, ec);
    std::filesystem::remove(configPath, ec);
}
