#include "core/CryptoUtil.h"

#include <openssl/evp.h>

#include <atomic>
#include <random>
#include <sstream>

namespace voterpool {
namespace {

std::string toHex(const unsigned char* data, size_t len) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0F]);
    }
    return out;
}

std::string randomHex(size_t nbytes) {
    std::string out;
    out.reserve(nbytes * 2);
    std::random_device rd;
    while (out.size() < nbytes * 2) {
        unsigned long long v = 0;
        for (int i = 0; i < 2; ++i) v = (v << 32) | rd();
        static const char* digits = "0123456789abcdef";
        for (size_t b = 0; b < sizeof(v) && out.size() < nbytes * 2; ++b) {
            out.push_back(digits[(v >> (b * 8)) & 0xF]);
            out.push_back(digits[(v >> (b * 8 + 4)) & 0xF]);
        }
    }
    return out;
}

}  // namespace

std::string sha256Hex(const std::string& input) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int mdLen = 0;
    EVP_Digest(input.data(), input.size(), md, &mdLen, EVP_sha256(), nullptr);
    return toHex(md, mdLen);
}

std::string generateUuidV4() {
    std::string h = randomHex(16);
    h[12] = '4';
    h[16] = "89ab"[h[16] - '0' & 3];
    return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" + h.substr(16, 4) + "-" + h.substr(20, 12);
}

std::string generateApiKey() { return "voterpool_sec_" + randomHex(24); }

bool isValidUuid(const std::string& s) {
    if (s.size() != 36) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

}  // namespace voterpool
