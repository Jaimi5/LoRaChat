#include "reportAuth.h"

#include <cstdio>

static const uint8_t REPORT_LABEL[] = {'L', 'M', 'R', '1'};

Sha256 reportKey(const DeploymentKey& key) {
    return hmacSha256(key.data(), key.size(), REPORT_LABEL, sizeof(REPORT_LABEL));
}

std::string reportTag(const Sha256& reportKey, const std::string& body) {
    Sha256 digest = hmacSha256(reportKey.data(), reportKey.size(),
                               reinterpret_cast<const uint8_t*>(body.data()), body.size());
    char hex[2 * sizeof(Sha256) + 1];
    for (size_t i = 0; i < digest.size(); i++) {
        snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    }
    return hex;
}
