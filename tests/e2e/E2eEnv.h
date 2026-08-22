#pragma once

#include "server/VoterpoolApp.h"

#include <string>

namespace voterpool::testing {

class E2eEnv {
public:
    static E2eEnv& instance();

    const std::string& host() const { return host_; }
    int port() const { return port_; }

private:
    E2eEnv();
    ~E2eEnv();

    std::string host_ = "127.0.0.1";
    int port_;
    std::string dbDir_;
    std::unique_ptr<VoterpoolApp> app_;
};

}  // namespace voterpool::testing
