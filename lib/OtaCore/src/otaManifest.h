#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "otaRecord.h"

using Sha256 = std::array<uint8_t, 32>;
/** ECDSA P-256 signature as raw r || s. */
using Signature = std::array<uint8_t, 64>;
/** Uncompressed P-256 public key: 0x04 || X || Y. */
using PublicKey = std::array<uint8_t, 65>;

/** Kind of payload described by a manifest. Persisted values, never renumber them. */
enum class ImageType : uint8_t {
    FULL_APP = 1,
};

/**
 * @brief Signed description of one firmware image.
 *
 * Travels as a 128-byte body followed by a 64-byte ECDSA P-256 signature over the body
 * (192 bytes in total). The image is identified by the SHA-256 that ESP-IDF appends to it.
 */
struct OtaManifest {
    static constexpr uint16_t FLAG_ALLOW_DOWNGRADE = 1u << 0;
    static constexpr uint16_t FLAG_SKIP_MESH_CHECK = 1u << 1;

    ImageType type = ImageType::FULL_APP;
    uint16_t flags = 0;
    uint32_t keyId = 0;
    /** MAJOR.MINOR.PATCH packed as 0xMMmmpp00. */
    uint32_t version = 0;
    uint32_t imageSize = 0;
    std::string boardEnv;
    std::string project;
    /** Full version string, as in esp_app_desc_t.version. */
    std::string versionString;
    Sha256 imageSha{};
    /** First bytes of the SHA-256 of the partition table the image was built for. */
    ShaPrefix partitionTablePrefix{};

    bool allowDowngrade() const { return (flags & FLAG_ALLOW_DOWNGRADE) != 0; }
    bool skipMeshCheck() const { return (flags & FLAG_SKIP_MESH_CHECK) != 0; }
    ShaPrefix imageShaPrefix() const;

    bool operator==(const OtaManifest& other) const;
};

/** Binary layout of the manifest body. */
class OtaManifestCodec {
public:
    static constexpr size_t BODY_SIZE = 128;
    static constexpr size_t SIGNATURE_SIZE = 64;
    static constexpr size_t SIGNED_SIZE = BODY_SIZE + SIGNATURE_SIZE;
    static constexpr uint8_t FORMAT_VERSION = 1;
    static constexpr size_t BOARD_ENV_SIZE = 16;
    static constexpr size_t PROJECT_SIZE = 16;
    static constexpr size_t VERSION_STRING_SIZE = 32;

    /**
     * @brief Encodes the 128-byte body.
     * @return the body, or an empty vector if a string does not fit its field (each field keeps
     *         room for a terminating zero).
     */
    static std::vector<uint8_t> encode(const OtaManifest& manifest);

    /** @return false if @p body is not a manifest body of this format. */
    static bool decode(const uint8_t* body, size_t size, OtaManifest& out);
};

/** A public key the node accepts manifests from. */
struct TrustedKey {
    uint32_t keyId;
    PublicKey publicKey;
    /** Test keys sign bench and CI images and are refused by production builds. */
    bool test;
};

/** Checks an ECDSA P-256 signature over SHA-256(@p message). */
using SignatureVerifier = std::function<bool(const PublicKey& key, const uint8_t* message,
                                             size_t size, const Signature& signature)>;

/** Result of checking a signed manifest. */
enum class ManifestCheck : uint8_t {
    OK = 0,
    TOO_SHORT = 1,
    BAD_FORMAT = 2,
    UNKNOWN_KEY = 3,
    TEST_KEY_REFUSED = 4,
    BAD_SIGNATURE = 5,
};

/**
 * @brief Verifies a signed manifest (body + signature) and decodes it.
 *
 * @p out is only written when the result is OK.
 */
ManifestCheck verifySignedManifest(const uint8_t* data, size_t size,
                                   const std::vector<TrustedKey>& keys, bool acceptTestKeys,
                                   const SignatureVerifier& verifier, OtaManifest& out);
