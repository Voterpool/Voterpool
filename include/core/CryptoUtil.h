#pragma once

#include <string>

namespace voterpool {

std::string sha256Hex(const std::string& input);

std::string generateUuidV4();

std::string generateApiKey();

bool isValidUuid(const std::string& s);

}  // namespace voterpool
