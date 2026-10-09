#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "otaManifest.h"
#include "otaStreamWriter.h"

/**
 * @brief Reads an update bundle: the 192-byte signed manifest followed by the image.
 *
 * Used for uploads through the node's access point. The manifest is checked, and the upload
 * length compared with it, before the first flash write; the image then goes through the same
 * OtaStreamWriter as the WiFi pull. After a failure every call returns that failure.
 */
class OtaBundleReader {
public:
    /** Verifies a signed manifest and the update policy. @return true to install it. */
    using ManifestGate =
        std::function<bool(const uint8_t* data, size_t size, OtaManifest& out)>;
    /** Called once all checks passed, right before the first flash write. */
    using BeforeWrite = std::function<void(const OtaManifest& manifest)>;

    OtaBundleReader(OtaStreamWriter& writer, ManifestGate gate, BeforeWrite beforeWrite = {})
        : writer_(writer), gate_(std::move(gate)), beforeWrite_(std::move(beforeWrite)) {}

    /** Starts a bundle whose upload body is @p contentLength bytes. */
    void begin(uint32_t contentLength);

    /** Consumes the next @p size bytes of the upload. */
    UpdateStatus push(const uint8_t* data, size_t size);

    /** Ends the upload. The image must be complete and match its manifest. */
    UpdateStatus finish();

    /** Drops the upload; the boot partition does not change. */
    void abort();

    /** @return the manifest, valid once the image is being written. */
    const OtaManifest& manifest() const { return manifest_; }

private:
    enum class Stage : uint8_t { MANIFEST, IMAGE, DONE };

    UpdateStatus fail(UpdateStatus status);
    UpdateStatus acceptManifest();

    OtaStreamWriter& writer_;
    ManifestGate gate_;
    BeforeWrite beforeWrite_;
    OtaManifest manifest_;
    std::vector<uint8_t> header_;
    uint32_t contentLength_ = 0;
    Stage stage_ = Stage::DONE;
    UpdateStatus status_ = UpdateStatus::INCOMPLETE;
};
