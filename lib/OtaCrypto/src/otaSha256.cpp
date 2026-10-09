#include "otaSha256.h"

MbedSha256::MbedSha256() { mbedtls_sha256_init(&context_); }

MbedSha256::~MbedSha256() { mbedtls_sha256_free(&context_); }

void MbedSha256::start() { mbedtls_sha256_starts_ret(&context_, 0); }

void MbedSha256::update(const uint8_t* data, size_t size) {
    mbedtls_sha256_update_ret(&context_, data, size);
}

Sha256 MbedSha256::finish() {
    Sha256 out{};
    mbedtls_sha256_finish_ret(&context_, out.data());
    return out;
}
