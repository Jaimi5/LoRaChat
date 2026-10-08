#include "otaManifest.h"

#include <algorithm>
#include <cstring>

namespace {

// Body layout (little-endian):
//   0  magic "LMOT"            24  board_env[16]
//   4  format version          40  project[16]
//   5  image type              56  version string[32]
//   6  flags u16               88  image SHA-256[32]
//   8  key id u32             120  partition table SHA-256 prefix[8]
//  12  version u32
//  16  image size u32
//  20  reserved u32 (zero)
constexpr char MAGIC[4] = {'L', 'M', 'O', 'T'};
constexpr size_t OFFSET_FORMAT = 4;
constexpr size_t OFFSET_TYPE = 5;
constexpr size_t OFFSET_FLAGS = 6;
constexpr size_t OFFSET_KEY_ID = 8;
constexpr size_t OFFSET_VERSION = 12;
constexpr size_t OFFSET_IMAGE_SIZE = 16;
constexpr size_t OFFSET_BOARD_ENV = 24;
constexpr size_t OFFSET_PROJECT = 40;
constexpr size_t OFFSET_VERSION_STRING = 56;
constexpr size_t OFFSET_IMAGE_SHA = 88;
constexpr size_t OFFSET_PARTITION_PREFIX = 120;

void putU16(std::vector<uint8_t>& out, size_t offset, uint16_t value) {
    out[offset] = static_cast<uint8_t>(value);
    out[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void putU32(std::vector<uint8_t>& out, size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4; i++) out[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

uint16_t getU16(const uint8_t* in, size_t offset) {
    return static_cast<uint16_t>(in[offset] | (in[offset + 1] << 8));
}

uint32_t getU32(const uint8_t* in, size_t offset) {
    uint32_t value = 0;
    for (size_t i = 0; i < 4; i++) value |= static_cast<uint32_t>(in[offset + i]) << (8 * i);
    return value;
}

bool putString(std::vector<uint8_t>& out, size_t offset, size_t field, const std::string& value) {
    if (value.size() >= field) return false;
    std::copy(value.begin(), value.end(), out.begin() + offset);
    return true;
}

bool getString(const uint8_t* in, size_t offset, size_t field, std::string& out) {
    const uint8_t* begin = in + offset;
    const uint8_t* end = std::find(begin, begin + field, 0);
    if (end == begin + field) return false;
    out.assign(begin, end);
    return true;
}

}  // namespace

constexpr uint16_t OtaManifest::FLAG_ALLOW_DOWNGRADE;
constexpr uint16_t OtaManifest::FLAG_SKIP_MESH_CHECK;
constexpr size_t OtaManifestCodec::BODY_SIZE;
constexpr size_t OtaManifestCodec::SIGNATURE_SIZE;
constexpr size_t OtaManifestCodec::SIGNED_SIZE;
constexpr uint8_t OtaManifestCodec::FORMAT_VERSION;
constexpr size_t OtaManifestCodec::BOARD_ENV_SIZE;
constexpr size_t OtaManifestCodec::PROJECT_SIZE;
constexpr size_t OtaManifestCodec::VERSION_STRING_SIZE;

ShaPrefix OtaManifest::imageShaPrefix() const {
    ShaPrefix prefix{};
    std::copy(imageSha.begin(), imageSha.begin() + prefix.size(), prefix.begin());
    return prefix;
}

bool OtaManifest::operator==(const OtaManifest& other) const {
    return type == other.type && flags == other.flags && keyId == other.keyId &&
           version == other.version && imageSize == other.imageSize &&
           boardEnv == other.boardEnv && project == other.project &&
           versionString == other.versionString && imageSha == other.imageSha &&
           partitionTablePrefix == other.partitionTablePrefix;
}

std::vector<uint8_t> OtaManifestCodec::encode(const OtaManifest& manifest) {
    std::vector<uint8_t> out(BODY_SIZE, 0);
    std::copy(MAGIC, MAGIC + sizeof(MAGIC), out.begin());
    out[OFFSET_FORMAT] = FORMAT_VERSION;
    out[OFFSET_TYPE] = static_cast<uint8_t>(manifest.type);
    putU16(out, OFFSET_FLAGS, manifest.flags);
    putU32(out, OFFSET_KEY_ID, manifest.keyId);
    putU32(out, OFFSET_VERSION, manifest.version);
    putU32(out, OFFSET_IMAGE_SIZE, manifest.imageSize);
    if (!putString(out, OFFSET_BOARD_ENV, BOARD_ENV_SIZE, manifest.boardEnv) ||
        !putString(out, OFFSET_PROJECT, PROJECT_SIZE, manifest.project) ||
        !putString(out, OFFSET_VERSION_STRING, VERSION_STRING_SIZE, manifest.versionString)) {
        return {};
    }
    std::copy(manifest.imageSha.begin(), manifest.imageSha.end(), out.begin() + OFFSET_IMAGE_SHA);
    std::copy(manifest.partitionTablePrefix.begin(), manifest.partitionTablePrefix.end(),
              out.begin() + OFFSET_PARTITION_PREFIX);
    return out;
}

bool OtaManifestCodec::decode(const uint8_t* body, size_t size, OtaManifest& out) {
    if (size != BODY_SIZE || std::memcmp(body, MAGIC, sizeof(MAGIC)) != 0 ||
        body[OFFSET_FORMAT] != FORMAT_VERSION) {
        return false;
    }
    OtaManifest m;
    m.type = static_cast<ImageType>(body[OFFSET_TYPE]);
    m.flags = getU16(body, OFFSET_FLAGS);
    m.keyId = getU32(body, OFFSET_KEY_ID);
    m.version = getU32(body, OFFSET_VERSION);
    m.imageSize = getU32(body, OFFSET_IMAGE_SIZE);
    if (!getString(body, OFFSET_BOARD_ENV, BOARD_ENV_SIZE, m.boardEnv) ||
        !getString(body, OFFSET_PROJECT, PROJECT_SIZE, m.project) ||
        !getString(body, OFFSET_VERSION_STRING, VERSION_STRING_SIZE, m.versionString)) {
        return false;
    }
    std::copy(body + OFFSET_IMAGE_SHA, body + OFFSET_IMAGE_SHA + m.imageSha.size(),
              m.imageSha.begin());
    std::copy(body + OFFSET_PARTITION_PREFIX,
              body + OFFSET_PARTITION_PREFIX + m.partitionTablePrefix.size(),
              m.partitionTablePrefix.begin());
    out = m;
    return true;
}

ManifestCheck verifySignedManifest(const uint8_t* data, size_t size,
                                   const std::vector<TrustedKey>& keys, bool acceptTestKeys,
                                   const SignatureVerifier& verifier, OtaManifest& out) {
    if (size < OtaManifestCodec::SIGNED_SIZE) return ManifestCheck::TOO_SHORT;

    OtaManifest manifest;
    if (!OtaManifestCodec::decode(data, OtaManifestCodec::BODY_SIZE, manifest)) {
        return ManifestCheck::BAD_FORMAT;
    }

    auto key = std::find_if(keys.begin(), keys.end(),
                            [&](const TrustedKey& k) { return k.keyId == manifest.keyId; });
    if (key == keys.end()) return ManifestCheck::UNKNOWN_KEY;
    if (key->test && !acceptTestKeys) return ManifestCheck::TEST_KEY_REFUSED;

    Signature signature{};
    std::copy(data + OtaManifestCodec::BODY_SIZE, data + OtaManifestCodec::SIGNED_SIZE,
              signature.begin());
    if (!verifier(key->publicKey, data, OtaManifestCodec::BODY_SIZE, signature)) {
        return ManifestCheck::BAD_SIGNATURE;
    }

    out = manifest;
    return ManifestCheck::OK;
}
