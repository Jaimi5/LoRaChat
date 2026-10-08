#include "otaKeys.h"

#include <algorithm>

#include "otaSignature.h"
#include "otaTestKey.h"

namespace {

TrustedKey makeKey(uint32_t keyId, const uint8_t (&publicKey)[65], bool test) {
    TrustedKey key{keyId, {}, test};
    std::copy(publicKey, publicKey + sizeof(publicKey), key.publicKey.begin());
    return key;
}

}  // namespace

const std::vector<TrustedKey>& otaTrustedKeys() {
    static const std::vector<TrustedKey> keys = {
        makeKey(OTA_TEST_KEY_ID, OTA_TEST_PUBLIC_KEY, true),
    };
    return keys;
}

bool otaAcceptTestKeys() {
#ifdef OTA_PRODUCTION
    return false;
#else
    return true;
#endif
}

bool otaSignaturePathWorks() {
    OtaManifest manifest;
    return verifySignedManifest(OTA_SELF_TEST_MANIFEST, sizeof(OTA_SELF_TEST_MANIFEST),
                                otaTrustedKeys(), true, verifyP256Sha256,
                                manifest) == ManifestCheck::OK;
}
