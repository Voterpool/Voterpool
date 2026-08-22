#pragma once

#include "core/Config.h"

#include <spdlog/spdlog.h>

namespace voterpool::Logger {

void init(const LoggingConfig& cfg);
void shutdown();

}  // namespace voterpool::Logger
