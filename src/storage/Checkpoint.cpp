#include "storage/Checkpoint.h"

#include "core/Config.h"
#include "core/Logger.h"
#include "storage/RocksDBWrapper.h"

#include <rocksdb/utilities/checkpoint.h>

#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace voterpool {

int runCheckpointCommand(int argc, char** argv) {
    std::string configPath = "./config.yaml";
    std::string outPath;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--config" && i + 1 < argc) configPath = argv[++i];
        else if (a == "--path" && i + 1 < argc) outPath = argv[++i];
    }
    if (outPath.empty()) {
        std::cerr << "checkpoint: --path <dir> is required\n";
        return 1;
    }
    std::vector<char*> loadArgs{const_cast<char*>("voterpool")};
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "checkpoint") continue;
        if (std::string(argv[i]) == "--path") {
            ++i;
            continue;
        }
        loadArgs.push_back(argv[i]);
    }
    try {
        AppConfig cfg = AppConfig::load(static_cast<int>(loadArgs.size()), loadArgs.data());
        RocksDBWrapper db(cfg.storage);
        if (!db.open()) {
            std::cerr << "checkpoint: cannot open database (locked, corrupted or unhealthy)\n";
            return 1;
        }
        if (!DbHealth::instance().healthy()) {
            std::cerr << "checkpoint: refusing to snapshot an unhealthy database\n";
            return 1;
        }
        rocksdb::Checkpoint* rawCheckpoint = nullptr;
        rocksdb::Status st = rocksdb::Checkpoint::Create(db.raw(), &rawCheckpoint);
        if (!st.ok()) {
            std::cerr << "checkpoint: Create failed: " << st.ToString() << "\n";
            return 1;
        }
        std::unique_ptr<rocksdb::Checkpoint> checkpoint(rawCheckpoint);
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(outPath).parent_path(), ec);
        std::filesystem::remove_all(outPath, ec);
        st = checkpoint->CreateCheckpoint(outPath);
        if (!st.ok()) {
            std::cerr << "checkpoint: snapshot failed: " << st.ToString() << "\n";
            return 1;
        }
        db.close();
        std::cout << "checkpoint created at " << outPath << "\n";
        return 0;
    } catch (const ConfigError& e) {
        std::cerr << "Configuration error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "checkpoint failed: " << e.what() << "\n";
        return 1;
    }
}

}  // namespace voterpool
