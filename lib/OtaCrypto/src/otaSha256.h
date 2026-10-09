#pragma once

#include "iOtaFlash.h"
#include "mbedtls/sha256.h"

/** Incremental SHA-256 with mbedTLS. */
class MbedSha256 : public ISha256 {
public:
    MbedSha256();
    ~MbedSha256() override;
    MbedSha256(const MbedSha256&) = delete;
    MbedSha256& operator=(const MbedSha256&) = delete;

    void start() override;
    void update(const uint8_t* data, size_t size) override;
    Sha256 finish() override;

private:
    mbedtls_sha256_context context_;
};
