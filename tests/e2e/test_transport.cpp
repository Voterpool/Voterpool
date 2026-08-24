#include "tests/common/HttpUtil.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>
#include <fcntl.h>
#include <random>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#ifndef VOTERPOOL_BIN
#define VOTERPOOL_BIN "./voterpool"
#endif

using namespace voterpool;
using namespace voterpool::testing;

namespace {

std::string tempDir(const char* tag) {
    return std::filesystem::temp_directory_path().string() + "/voterpool_transport_" + tag + "_" +
           std::to_string(getpid()) + "_" + std::to_string(std::random_device{}());
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

void writeConfig(const std::string& path, int port, const std::string& dbDir,
                 const std::string& extraServer = {}, const std::string& extraStorage = {}) {
    std::ofstream out(path);
    out << "server:\n  host: \"127.0.0.1\"\n  port: " << port << "\n" << extraServer;
    out << "storage:\n  path: \"" << dbDir << "\"\n" << extraStorage;
}

// Запускает сервер в subprocess и ждёт готовности /health.
pid_t spawnServer(const std::string& configPath, int port, int timeoutMs = 20000) {
    pid_t pid = fork();
    if (pid == 0) {
        std::string logPath = configPath + ".child.log";
        int logFd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (logFd >= 0) {
            dup2(logFd, STDOUT_FILENO);
            dup2(logFd, STDERR_FILENO);
            close(logFd);
        }
        execl(VOTERPOOL_BIN, VOTERPOOL_BIN, "--config", configPath.c_str(), (char*)nullptr);
        int efd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (efd >= 0) {
            dprintf(efd, "EXEC FAILED errno=%d bin=%s\n", errno, VOTERPOOL_BIN);
            close(efd);
        }
        _exit(127);
    }
    EXPECT_TRUE(HttpUtil::waitForHttp("127.0.0.1", port, "/health", timeoutMs))
        << "server did not start on port " << port;
    return pid;
}

void stopServer(pid_t pid) {
    kill(pid, SIGTERM);
    int status = 0;
    for (int waited = 0; waitpid(pid, &status, WNOHANG) == 0 && waited < 15000; waited += 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool haveOpenssl() {
    return std::system("command -v openssl >/dev/null 2>&1") == 0;
}

std::string slurp(const std::string& cmd) {
    std::string out;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[512];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    pclose(p);
    return out;
}

}  // namespace

TEST(E2eTransport, PlaintextByDefaultAndOversizedBodyRejected) {
    std::string dbDir = tempDir("plain");
    int port = freePort();
    std::string configPath = dbDir + ".yaml";
    writeConfig(configPath, port, dbDir, "  max_request_body_size: 64\n");

    pid_t pid = spawnServer(configPath, port);
    ASSERT_GT(pid, 0);

    HttpResponse health = HttpUtil::get("127.0.0.1", port, "/health");
    ASSERT_EQ(health.status, 200);

    // Тело больше лимита отклоняется без разбора JSON-RPC и без учёта
    // инструментальных метрик.
    std::string big(4096, 'x');
    Json::Value body;
    body["jsonrpc"] = "2.0";
    body["id"] = 1;
    body["method"] = "tools/call";
    body["params"]["name"] = "register_agent";
    body["params"]["arguments"]["name"] = big;
    HttpResponse resp = HttpUtil::postJson("127.0.0.1", port, "/mcp", body,
                                           {{"Content-Type", "application/json"},
                                            {"MCP-Protocol-Version", "2026-07-28"},
                                            {"Mcp-Method", "tools/call"},
                                            {"Mcp-Name", "register_agent"}});
    EXPECT_NE(resp.status, 200) << "oversized body must be rejected";

    stopServer(pid);

    std::error_code ec;
    std::filesystem::remove_all(dbDir, ec);
    std::filesystem::remove(configPath, ec);
}

TEST(E2eTransport, SslEnabledWithoutReadableFilesIsFatal) {
    std::string dbDir = tempDir("sslbad");
    int port = freePort();
    std::string configPath = dbDir + ".yaml";
    writeConfig(configPath, port, dbDir,
                "  ssl:\n    enabled: true\n    cert_path: \"/nonexistent/voterpool.crt\"\n"
                "    key_path: \"/nonexistent/voterpool.key\"\n");

    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        execl(VOTERPOOL_BIN, VOTERPOOL_BIN, "--config", configPath.c_str(), (char*)nullptr);
        _exit(127);
    }
    int status = 0;
    int waited = 0;
    while (waitpid(pid, &status, WNOHANG) == 0 && waited < 10000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waited += 100;
    }
    ASSERT_NE(waitpid(pid, &status, WNOHANG), 0) << "process must exit fast on invalid ssl config";
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 1);

    std::error_code ec;
    std::filesystem::remove_all(dbDir, ec);
    std::filesystem::remove(configPath, ec);
}

TEST(E2eTransport, TlsListenerServesHealthOverSelfSignedCert) {
    if (!haveOpenssl()) GTEST_SKIP() << "openssl CLI unavailable";

    std::string dir = tempDir("tls");
    std::filesystem::create_directories(dir);
    const std::string cert = dir + "/cert.pem";
    const std::string key = dir + "/key.pem";
    const std::string gen =
        "openssl req -x509 -newkey rsa:2048 -keyout " + key + " -out " + cert +
        " -days 2 -nodes -subj '/CN=127.0.0.1' >/dev/null 2>&1";
    ASSERT_EQ(std::system(gen.c_str()), 0) << "failed to generate self-signed certificate";

    std::string dbDir = tempDir("tlsdb");
    int port = freePort();
    std::string configPath = dbDir + ".yaml";
    writeConfig(configPath, port, dbDir,
                "  ssl:\n    enabled: true\n    cert_path: \"" + cert + "\"\n    key_path: \"" + key +
                    "\"\n");

    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        execl(VOTERPOOL_BIN, VOTERPOOL_BIN, "--config", configPath.c_str(), (char*)nullptr);
        _exit(127);
    }

    // HTTPS /health через s_client (self-signed без верификации цепочки).
    bool gotOk = false;
    for (int attempt = 0; attempt < 40 && !gotOk; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        const std::string cmd = "printf 'GET /health HTTP/1.1\\r\\nHost: 127.0.0.1\\r\\nConnection:"
                                " close\\r\\n\\r\\n' | openssl s_client -connect 127.0.0.1:" +
                                std::to_string(port) + " -quiet 2>/dev/null";
        gotOk = slurp(cmd).find("200 OK") != std::string::npos;
    }
    ASSERT_TRUE(gotOk) << "TLS listener did not serve /health over https";

    stopServer(pid);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::remove_all(dbDir, ec);
    std::filesystem::remove(configPath, ec);
}
