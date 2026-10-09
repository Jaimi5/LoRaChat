#include "otaStreamWriter.h"

#include <algorithm>

#include "otaPolicy.h"

constexpr size_t OtaStreamWriter::DIGEST_SIZE;
constexpr size_t OtaStreamWriter::MIN_BODY_SIZE;

const char* updateStatusName(UpdateStatus status) {
    switch (status) {
        case UpdateStatus::OK:
            return "OK";
        case UpdateStatus::BAD_SIZE:
            return "BAD_SIZE";
        case UpdateStatus::BEGIN_FAILED:
            return "BEGIN_FAILED";
        case UpdateStatus::TOO_LONG:
            return "TOO_LONG";
        case UpdateStatus::WRITE_FAILED:
            return "WRITE_FAILED";
        case UpdateStatus::INCOMPLETE:
            return "INCOMPLETE";
        case UpdateStatus::SHA_MISMATCH:
            return "SHA_MISMATCH";
        case UpdateStatus::END_FAILED:
            return "END_FAILED";
        case UpdateStatus::READBACK_MISMATCH:
            return "READBACK_MISMATCH";
        case UpdateStatus::DESCRIPTOR_MISMATCH:
            return "DESCRIPTOR_MISMATCH";
        case UpdateStatus::TRANSPORT_FAILED:
            return "TRANSPORT_FAILED";
    }
    return "UNKNOWN";
}

UpdateStatus OtaStreamWriter::begin(const OtaManifest& manifest) {
    abort();
    if (manifest.imageSize < MIN_BODY_SIZE + DIGEST_SIZE) return UpdateStatus::BAD_SIZE;
    if (!flash_.begin(manifest.imageSize)) return UpdateStatus::BEGIN_FAILED;
    manifest_ = manifest;
    written_ = 0;
    active_ = true;
    sha_.start();
    return UpdateStatus::OK;
}

UpdateStatus OtaStreamWriter::write(const uint8_t* data, size_t size) {
    if (!active_) return UpdateStatus::WRITE_FAILED;
    if (size > manifest_.imageSize - written_) return fail(UpdateStatus::TOO_LONG);

    // Everything before the appended digest is hashed; the digest itself is kept to compare.
    const uint32_t bodySize = manifest_.imageSize - DIGEST_SIZE;
    size_t hashed = written_ < bodySize ? std::min<size_t>(size, bodySize - written_) : 0;
    sha_.update(data, hashed);
    if (hashed < size) {
        size_t tailOffset = written_ + hashed - bodySize;
        std::copy(data + hashed, data + size, tail_.begin() + tailOffset);
    }

    if (!flash_.write(data, size)) return fail(UpdateStatus::WRITE_FAILED);
    written_ += static_cast<uint32_t>(size);
    return UpdateStatus::OK;
}

UpdateStatus OtaStreamWriter::finish() {
    if (!active_ || written_ != manifest_.imageSize) return fail(UpdateStatus::INCOMPLETE);
    if (sha_.finish() != manifest_.imageSha || tail_ != manifest_.imageSha) {
        return fail(UpdateStatus::SHA_MISMATCH);
    }

    active_ = false;
    if (!flash_.end()) return UpdateStatus::END_FAILED;
    Sha256 readBack{};
    if (!flash_.writtenSha(readBack) || readBack != manifest_.imageSha) {
        return UpdateStatus::READBACK_MISMATCH;
    }
    ImageDescriptor descriptor;
    if (!flash_.readDescriptor(descriptor) ||
        !OtaPolicy::descriptorMatches(manifest_, descriptor.project, descriptor.version)) {
        return UpdateStatus::DESCRIPTOR_MISMATCH;
    }
    return UpdateStatus::OK;
}

void OtaStreamWriter::abort() {
    if (!active_) return;
    active_ = false;
    flash_.abort();
}

UpdateStatus OtaStreamWriter::fail(UpdateStatus status) {
    abort();
    return status;
}
