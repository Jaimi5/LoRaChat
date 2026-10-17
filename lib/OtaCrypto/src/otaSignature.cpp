#include "otaSignature.h"

#include "mbedtls/ecdsa.h"
#include "mbedtls/sha256.h"

bool verifyP256Sha256(const PublicKey& key, const uint8_t* message, size_t size,
                      const Signature& signature) {
    uint8_t hash[32];
    if (mbedtls_sha256_ret(message, size, hash, 0) != 0) return false;

    mbedtls_ecp_group group;
    mbedtls_ecp_point point;
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&point);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    const size_t half = signature.size() / 2;
    bool valid = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
                 mbedtls_ecp_point_read_binary(&group, &point, key.data(), key.size()) == 0 &&
                 mbedtls_ecp_check_pubkey(&group, &point) == 0 &&
                 mbedtls_mpi_read_binary(&r, signature.data(), half) == 0 &&
                 mbedtls_mpi_read_binary(&s, signature.data() + half, half) == 0 &&
                 mbedtls_ecdsa_verify(&group, hash, sizeof(hash), &point, &r, &s) == 0;

    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&point);
    mbedtls_ecp_group_free(&group);
    return valid;
}
