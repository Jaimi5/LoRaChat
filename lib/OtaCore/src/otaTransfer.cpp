#include "otaTransfer.h"

constexpr uint8_t OtaTransfer::MAX_ATTEMPTS;

TransferResult OtaTransfer::run(IImageSource& source, OtaStreamWriter& writer,
                                const OtaManifest& manifest, uint8_t maxAttempts) {
    TransferResult result;
    while (result.attempts < maxAttempts) {
        result.attempts++;
        result.status = writer.begin(manifest);
        if (result.status != UpdateStatus::OK) return result;

        UpdateStatus writeStatus = UpdateStatus::OK;
        bool transportOk = source.fetch([&](const uint8_t* data, size_t size) {
            result.bytes += static_cast<uint32_t>(size);
            writeStatus = writer.write(data, size);
            return writeStatus == UpdateStatus::OK;
        });
        if (writeStatus != UpdateStatus::OK) {
            result.status = writeStatus;
            return result;
        }
        if (!transportOk) {
            writer.abort();
            continue;
        }
        result.status = writer.finish();
        if (result.status != UpdateStatus::INCOMPLETE) return result;
    }
    result.status = UpdateStatus::TRANSPORT_FAILED;
    return result;
}
