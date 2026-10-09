#include "deploymentKey.h"

#include <algorithm>
#include <cstdio>

#include "mbedtls/md.h"
#include "mbedtls/sha256.h"

static const uint8_t AP_LABEL[] = {'L', 'M', 'A', 'P', '1'};
static constexpr size_t AP_PASSWORD_BYTES = 8;

Sha256 hmacSha256(const uint8_t* key, size_t keySize, const uint8_t* data, size_t size) {
    Sha256 out{};
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, keySize, data, size,
                    out.data());
    return out;
}

std::string apSsid(uint16_t nodeAddress) {
    char ssid[8];
    snprintf(ssid, sizeof(ssid), "LM-%04X", nodeAddress);
    return ssid;
}

std::string apPassword(const DeploymentKey& key, uint16_t nodeAddress) {
    uint8_t message[sizeof(AP_LABEL) + 2];
    std::copy(AP_LABEL, AP_LABEL + sizeof(AP_LABEL), message);
    message[sizeof(AP_LABEL)] = static_cast<uint8_t>(nodeAddress & 0xFF);
    message[sizeof(AP_LABEL) + 1] = static_cast<uint8_t>(nodeAddress >> 8);
    Sha256 digest = hmacSha256(key.data(), key.size(), message, sizeof(message));

    char password[2 * AP_PASSWORD_BYTES + 1];
    for (size_t i = 0; i < AP_PASSWORD_BYTES; i++) {
        snprintf(password + 2 * i, 3, "%02x", digest[i]);
    }
    return password;
}

static int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parseDeploymentKey(const std::string& text, DeploymentKey& out) {
    const char* spaces = " \t\r\n";
    size_t first = text.find_first_not_of(spaces);
    if (first == std::string::npos) return false;
    size_t last = text.find_last_not_of(spaces);
    if (last - first + 1 != 2 * out.size()) return false;

    DeploymentKey key{};
    for (size_t i = 0; i < key.size(); i++) {
        int high = hexValue(text[first + 2 * i]);
        int low = hexValue(text[first + 2 * i + 1]);
        if (high < 0 || low < 0) return false;
        key[i] = static_cast<uint8_t>(high << 4 | low);
    }
    if (std::all_of(key.begin(), key.end(), [](uint8_t b) { return b == 0; })) return false;
    out = key;
    return true;
}

std::string keyFingerprint(const DeploymentKey& key) {
    uint8_t digest[32];
    mbedtls_sha256_ret(key.data(), key.size(), digest, 0);
    char text[9];
    snprintf(text, sizeof(text), "%02x%02x%02x%02x", digest[0], digest[1], digest[2], digest[3]);
    return text;
}
