#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "otaStreamWriter.h"

/** A transport that delivers the image bytes, always from byte 0. */
class IImageSource {
public:
    using Sink = std::function<bool(const uint8_t* data, size_t size)>;

    virtual ~IImageSource() = default;

    /**
     * @brief Streams the whole image into @p sink, chunk by chunk.
     *
     * Stops early when @p sink returns false.
     * @return false on a transport error (connection lost, timeout, HTTP error).
     */
    virtual bool fetch(const Sink& sink) = 0;
};

/** Outcome of OtaTransfer::run(). */
struct TransferResult {
    UpdateStatus status = UpdateStatus::TRANSPORT_FAILED;
    uint8_t attempts = 0;
    /** Bytes received over all attempts. */
    uint32_t bytes = 0;
};

/**
 * @brief Downloads an image into the update slot, restarting from byte 0 when the transport
 *        is cut.
 *
 * Only transport errors and short bodies are retried. An image that is too long, does not
 * hash to the manifest or cannot be written fails at once, because a new attempt would get the
 * same bytes.
 */
class OtaTransfer {
public:
    static constexpr uint8_t MAX_ATTEMPTS = 3;

    static TransferResult run(IImageSource& source, OtaStreamWriter& writer,
                              const OtaManifest& manifest, uint8_t maxAttempts = MAX_ATTEMPTS);
};
