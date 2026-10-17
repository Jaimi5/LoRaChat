#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "fakeFlash.h"
#include "mbedtls/sha256.h"
#include "otaManifest.h"
#include "otaStreamWriter.h"

constexpr size_t SLOT_SIZE = 8 * FakeFlash::SECTOR_SIZE;
// Spans several sectors and does not end on a chunk or sector boundary.
constexpr size_t IMAGE_SIZE = 3 * FakeFlash::SECTOR_SIZE + 1000;

struct TestImage {
    std::vector<uint8_t> bytes;
    OtaManifest manifest;
};

inline void putField(std::vector<uint8_t>& body, size_t offset, const std::string& text) {
    std::fill(body.begin() + offset, body.begin() + offset + FakeFlash::DESC_FIELD_SIZE, 0);
    std::copy(text.begin(), text.end(), body.begin() + offset);
}

/** An image laid out like an ESP-IDF build: header, app descriptor, appended SHA-256. */
inline TestImage makeImage(size_t size = IMAGE_SIZE, const std::string& version = "0.1.3+gabc1234",
                    const std::string& project = "LoRaChat") {
    std::vector<uint8_t> body(size - OtaStreamWriter::DIGEST_SIZE);
    for (size_t i = 0; i < body.size(); i++) body[i] = static_cast<uint8_t>(i * 31 + 7);
    body[0] = 0xE9;
    const uint8_t descMagic[] = {0x32, 0x54, 0xCD, 0xAB};
    std::copy(descMagic, descMagic + 4, body.begin() + FakeFlash::DESC_OFFSET);
    putField(body, FakeFlash::DESC_VERSION_OFFSET, version);
    putField(body, FakeFlash::DESC_PROJECT_OFFSET, project);

    TestImage image;
    mbedtls_sha256_ret(body.data(), body.size(), image.manifest.imageSha.data(), 0);
    image.bytes = body;
    image.bytes.insert(image.bytes.end(), image.manifest.imageSha.begin(),
                       image.manifest.imageSha.end());
    image.manifest.keyId = 1;
    image.manifest.version = 0x00010300;
    image.manifest.imageSize = static_cast<uint32_t>(size);
    image.manifest.boardEnv = "tbeam";
    image.manifest.project = "LoRaChat";
    image.manifest.versionString = "0.1.3+gabc1234";
    return image;
}
