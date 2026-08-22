#include "tests/e2e/E2eEnv.h"

#include "tests/common/HttpUtil.h"

#include <drogon/drogon.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <thread>

namespace voterpool::testing {
namespace {

int pickFreePort() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return 18099;
    }
    socklen_t len = sizeof(addr);
    getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    int port = ntohs(addr.sin_port);
    close(fd);
    return port;
}

}  // namespace

E2eEnv& E2eEnv::instance() {
    static E2eEnv env;
    return env;
}

E2eEnv::E2eEnv() : port_(pickFreePort()) {
    dbDir_ = std::filesystem::temp_directory_path().string() + "/voterpool_e2e_" +
             std::to_string(getpid());
    std::filesystem::remove_all(dbDir_);

    AppConfig cfg;
    cfg.server.host = host_;
    cfg.server.port = port_;
    cfg.server.threads_num = 2;
    cfg.storage.path = dbDir_;
    cfg.sse.heartbeat_interval_sec = 1;

    app_ = std::make_unique<VoterpoolApp>(cfg);
    if (!app_->init()) {
        abort();
    }
    app_->startWorkers();
    std::thread serverThread([this]() { app_->run(); });
    serverThread.detach();

    HttpUtil::waitForHttp(host_, port_, "/health", 15000);
}

E2eEnv::~E2eEnv() {
    if (app_) {
        drogon::app().quit();
        if (app_->context().workers) app_->context().workers->stop();
        app_->context().workers.reset();
        app_->context().hub->shutdownAll();
        if (app_->context().db) app_->context().db->close();
        app_.reset();
        std::filesystem::remove_all(dbDir_);
    }
}

}  // namespace voterpool::testing
