#pragma once

#include <vector>

#include "otaManifest.h"

/** @return the keys this node accepts OTA manifests from. */
const std::vector<TrustedKey>& otaTrustedKeys();

/**
 * @return true if images signed with a test key may be installed. Production builds
 *         (OTA_PRODUCTION) refuse them.
 */
bool otaAcceptTestKeys();

/**
 * @brief Verifies the signed manifest embedded for the boot self-test.
 * @return true if the signature path (manifest decoding, key lookup, mbedTLS ECDSA) works.
 */
bool otaSignaturePathWorks();
