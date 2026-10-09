#include "otaBundle.h"

#include <algorithm>

void OtaBundleReader::begin(uint32_t contentLength) {
    abort();
    contentLength_ = contentLength;
    header_.clear();
    stage_ = Stage::MANIFEST;
    status_ = contentLength < OtaManifestCodec::SIGNED_SIZE ? UpdateStatus::BAD_BUNDLE_LENGTH
                                                            : UpdateStatus::OK;
}

UpdateStatus OtaBundleReader::push(const uint8_t* data, size_t size) {
    if (status_ != UpdateStatus::OK) return status_;
    if (stage_ == Stage::MANIFEST) {
        size_t take = std::min(size, OtaManifestCodec::SIGNED_SIZE - header_.size());
        header_.insert(header_.end(), data, data + take);
        data += take;
        size -= take;
        if (header_.size() < OtaManifestCodec::SIGNED_SIZE) return UpdateStatus::OK;
        UpdateStatus accepted = acceptManifest();
        if (accepted != UpdateStatus::OK) return fail(accepted);
    }
    if (stage_ != Stage::IMAGE) return fail(UpdateStatus::TOO_LONG);
    if (size == 0) return UpdateStatus::OK;
    UpdateStatus written = writer_.write(data, size);
    return written == UpdateStatus::OK ? written : fail(written);
}

UpdateStatus OtaBundleReader::acceptManifest() {
    if (!gate_(header_.data(), header_.size(), manifest_)) return UpdateStatus::MANIFEST_REJECTED;
    if (contentLength_ != OtaManifestCodec::SIGNED_SIZE + uint64_t{manifest_.imageSize}) {
        return UpdateStatus::BAD_BUNDLE_LENGTH;
    }
    if (beforeWrite_) beforeWrite_(manifest_);
    UpdateStatus started = writer_.begin(manifest_);
    if (started == UpdateStatus::OK) stage_ = Stage::IMAGE;
    return started;
}

UpdateStatus OtaBundleReader::finish() {
    if (status_ != UpdateStatus::OK) return status_;
    if (stage_ != Stage::IMAGE) return fail(UpdateStatus::INCOMPLETE);
    stage_ = Stage::DONE;
    status_ = writer_.finish();
    return status_;
}

void OtaBundleReader::abort() {
    if (stage_ == Stage::IMAGE) writer_.abort();
    if (stage_ != Stage::DONE) status_ = UpdateStatus::INCOMPLETE;
    stage_ = Stage::DONE;
}

UpdateStatus OtaBundleReader::fail(UpdateStatus status) {
    if (stage_ == Stage::IMAGE) writer_.abort();
    stage_ = Stage::DONE;
    status_ = status;
    return status;
}
