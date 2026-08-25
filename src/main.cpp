#include "core/Config.h"
#include "core/Logger.h"
#include "core/Memory.h"
#include "server/VoterpoolApp.h"
#include "storage/Checkpoint.h"
#include "storage/SchemaVersion.h"

#include <drogon/drogon.h>

#include "storage/RocksDBWrapper.h"
#include "storage/SchemaVersion.h"

#include <filesystem>
#include <unistd.h>

int main(int argc, char** argv) {
    voterpool::Memory::configure();

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "checkpoint") return voterpool::runCheckpointCommand(argc, argv);
    }

    try {
        voterpool::AppConfig cfg = voterpool::AppConfig::load(argc, argv);

        std::error_code ec;
        std::filesystem::create_directories(cfg.storage.path, ec);

        if (access(cfg.storage.path.c_str(), R_OK | W_OK) != 0) {
            std::cerr << "FATAL: no read/write access to storage path " << cfg.storage.path << "\n";
            return 1;
        }

        // Dry-run миграции схемы (tasks 3.2/3.4): валидация и отчёт без записи.
        bool migrateDryRun = false;
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--migrate-dry-run") migrateDryRun = true;
        }
        if (migrateDryRun) {
            voterpool::Logger::init(cfg.logging);
            voterpool::RocksDBWrapper db(cfg.storage);
            if (!db.open()) {
                std::cerr << "FATAL: cannot open storage at " << cfg.storage.path << "\n";
                return 1;
            }
            voterpool::SchemaManager mgr(db, /*dryRun=*/true);
            const auto gate = mgr.run();
            if (gate == voterpool::SchemaGateResult::kFatalNewerSchema ||
                gate == voterpool::SchemaGateResult::kError) {
                std::cerr << "FATAL: migration validation failed\n";
                return 1;
            }
            std::cout << "[dry-run] complete; database NOT modified\n";
            return 0;
        }

        voterpool::Logger::init(cfg.logging);

        voterpool::VoterpoolApp app(std::move(cfg));
        if (!app.init()) {
            return 1;
        }
        app.startWorkers();

        int rc = app.run();
        app.finalizeShutdown();
        voterpool::Logger::shutdown();
        return rc;
    } catch (const voterpool::ConfigError& e) {
        std::cerr << "Configuration error: " << e.what() << "\n";
        return 1;
    }
}
