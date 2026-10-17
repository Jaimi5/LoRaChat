#pragma once

#include <cstddef>
#include <cstdint>

#include "otaManifest.h"

/**
 * @brief Checks an ECDSA P-256 signature (raw r || s) over SHA-256(@p message) with mbedTLS.
 *
 * Has the signature of SignatureVerifier, so it can be passed to verifySignedManifest().
 */
bool verifyP256Sha256(const PublicKey& key, const uint8_t* message, size_t size,
                      const Signature& signature);
