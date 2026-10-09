#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "otaManifest.h"

/** Identity of an app image as stored in its esp_app_desc_t. */
struct ImageDescriptor {
    std::string project;
    std::string version;
};

/**
 * @brief The app slot an update is written to.
 *
 * The node implementation wraps esp_ota_* on the next update partition; native tests use a
 * fake NOR flash. The boot partition only changes in activate().
 */
class IOtaFlash {
public:
    virtual ~IOtaFlash() = default;

    /** Starts writing an image of @p size bytes at offset 0, erasing sectors as it goes. */
    virtual bool begin(uint32_t size) = 0;

    /** Appends @p size bytes to the image. */
    virtual bool write(const uint8_t* data, size_t size) = 0;

    /** Completes the write. The platform validates the image (header, checksum, digest). */
    virtual bool end() = 0;

    /** Drops a write in progress. Only valid between begin() and end(). */
    virtual void abort() = 0;

    /** Reads back the SHA-256 of the written image, as esp_partition_get_sha256() reports it. */
    virtual bool writtenSha(Sha256& out) = 0;

    /** Reads the descriptor of the written image. */
    virtual bool readDescriptor(ImageDescriptor& out) = 0;

    /** Selects the written image for the next boot. */
    virtual bool activate() = 0;
};

/** Incremental SHA-256. */
class ISha256 {
public:
    virtual ~ISha256() = default;

    virtual void start() = 0;
    virtual void update(const uint8_t* data, size_t size) = 0;
    virtual Sha256 finish() = 0;
};
