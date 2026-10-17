#pragma once

#include <cstddef>
#include <cstdint>

#include "iOtaFlash.h"
#include "otaManifest.h"

/** Result of writing an update. Values are reported, never renumber them. */
enum class UpdateStatus : uint8_t {
    OK = 0,
    /** The manifest size cannot hold an image with its appended digest. */
    BAD_SIZE = 1,
    BEGIN_FAILED = 2,
    /** More bytes arrived than the manifest announced. */
    TOO_LONG = 3,
    WRITE_FAILED = 4,
    /** Fewer bytes arrived than the manifest announced. */
    INCOMPLETE = 5,
    /** The streamed bytes do not hash to the manifest's image SHA-256. */
    SHA_MISMATCH = 6,
    END_FAILED = 7,
    /** The image read back from flash does not hash to the manifest's image SHA-256. */
    READBACK_MISMATCH = 8,
    /** The written image's project or version differs from the manifest. */
    DESCRIPTOR_MISMATCH = 9,
    /** Every download attempt was cut. */
    TRANSPORT_FAILED = 10,
    /** The upload length is not the manifest size plus the image size. */
    BAD_BUNDLE_LENGTH = 11,
    /** The manifest is not signed by a trusted key or the update policy refuses it. */
    MANIFEST_REJECTED = 12,
};

/** @return a short name of @p status for logs and reports. */
const char* updateStatusName(UpdateStatus status);

/**
 * @brief Streams one image into the update slot and checks it against its manifest.
 *
 * The image is the file built by ESP-IDF: its last 32 bytes are the SHA-256 of everything
 * before them, and that digest is the manifest's image SHA-256. Every failure before
 * IOtaFlash::end() aborts the write, so the boot partition never changes. Shared by the pull
 * over WiFi and the upload through the node's AP.
 */
class OtaStreamWriter {
public:
    static constexpr size_t DIGEST_SIZE = 32;
    /** Image header, first segment header and esp_app_desc_t. */
    static constexpr size_t MIN_BODY_SIZE = 24 + 8 + 256;

    OtaStreamWriter(IOtaFlash& flash, ISha256& sha) : flash_(flash), sha_(sha) {}

    /** Starts writing the image described by @p manifest. */
    UpdateStatus begin(const OtaManifest& manifest);

    /** Writes the next @p size bytes of the image. */
    UpdateStatus write(const uint8_t* data, size_t size);

    /** Completes the write and checks digest, read-back hash and descriptor. */
    UpdateStatus finish();

    /** Drops a write in progress. Does nothing if none is. */
    void abort();

    /** @return bytes written since begin(). */
    uint32_t written() const { return written_; }

private:
    UpdateStatus fail(UpdateStatus status);

    IOtaFlash& flash_;
    ISha256& sha_;
    OtaManifest manifest_;
    Sha256 tail_{};
    uint32_t written_ = 0;
    bool active_ = false;
};
