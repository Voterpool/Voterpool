#pragma once

#include "server/AppContext.h"
#include "server/AuthMiddleware.h"

namespace voterpool {

class VoterpoolApp {
public:
    explicit VoterpoolApp(AppConfig cfg) { ctx_.config = std::move(cfg); }

    bool init(IClock* clockOverride = nullptr);
    void registerRoutes();
    void startWorkers();
    int run();
    void finalizeShutdown();

    AppContext& context() { return ctx_; }

private:
    AppContext ctx_;
    std::unique_ptr<AuthMiddleware> middleware_;
};

}  // namespace voterpool
