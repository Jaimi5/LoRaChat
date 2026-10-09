#include <gtest/gtest.h>

#include <algorithm>

#include "apPassVectors.h"
#include "deploymentKey.h"
#include "manifestVectors.h"
#include "otaManifest.h"
#include "otaSha256.h"
#include "otaSignature.h"

namespace {

std::vector<TrustedKey> testKeys() {
    TrustedKey key{TEST_KEY_ID, {}, true};
    std::copy(TEST_PUBLIC_KEY, TEST_PUBLIC_KEY + sizeof(TEST_PUBLIC_KEY), key.publicKey.begin());
    return {key};
}

ManifestCheck check(const uint8_t* vector, bool acceptTestKeys, OtaManifest& out) {
    return verifySignedManifest(vector, OtaManifestCodec::SIGNED_SIZE, testKeys(),
                                acceptTestKeys, verifyP256Sha256, out);
}

}  // namespace

// Vectors are signed by scripts/ota_tools/sign_manifest.py with test/vectors/test_key.pem.
TEST(OtaCrypto, AcceptsAManifestSignedByPython) {
    OtaManifest m;
    ASSERT_EQ(check(VECTOR_VALID, true, m), ManifestCheck::OK);
    EXPECT_EQ(m.boardEnv, "tbeam");
    EXPECT_EQ(m.project, "LoRaChat");
    EXPECT_EQ(m.versionString, "0.1.3+gabc1234");
    EXPECT_EQ(m.version, 0x00010300u);
    EXPECT_EQ(m.imageSize, 1182800u);
    EXPECT_TRUE(m.skipMeshCheck());
    EXPECT_EQ(m.imageSha[31], 31);
    EXPECT_EQ(m.partitionTablePrefix[0], 0xA0);
}

TEST(OtaCrypto, RejectsATamperedBody) {
    OtaManifest m;
    EXPECT_EQ(check(VECTOR_TAMPERED_BODY, true, m), ManifestCheck::BAD_SIGNATURE);
}

TEST(OtaCrypto, RejectsASignatureOfAnotherKey) {
    OtaManifest m;
    EXPECT_EQ(check(VECTOR_SIGNED_BY_OTHER_KEY, true, m), ManifestCheck::BAD_SIGNATURE);
}

TEST(OtaCrypto, RejectsAnUnsignedManifest) {
    OtaManifest m;
    EXPECT_EQ(check(VECTOR_UNSIGNED, true, m), ManifestCheck::BAD_SIGNATURE);
}

TEST(OtaCrypto, ProductionRefusesTheTestKey) {
    OtaManifest m;
    EXPECT_EQ(check(VECTOR_VALID, false, m), ManifestCheck::TEST_KEY_REFUSED);
}

TEST(OtaCrypto, RejectsAPublicKeyThatIsNotOnTheCurve) {
    PublicKey key{};
    key[0] = 0x04;
    Signature signature{};
    std::copy(VECTOR_VALID + 128, VECTOR_VALID + 192, signature.begin());
    EXPECT_FALSE(verifyP256Sha256(key, VECTOR_VALID, 128, signature));
}

TEST(OtaCrypto, Sha256MatchesTheStandardVectorInAnySplit) {
    // FIPS 180-2 test vector for "abc".
    const Sha256 expected = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                             0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                             0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    const uint8_t abc[] = {'a', 'b', 'c'};
    MbedSha256 sha;
    sha.start();
    sha.update(abc, 3);
    EXPECT_EQ(sha.finish(), expected);

    sha.start();
    sha.update(abc, 1);
    sha.update(abc + 1, 0);
    sha.update(abc + 1, 2);
    EXPECT_EQ(sha.finish(), expected);
}

TEST(OtaCrypto, HmacSha256MatchesRfc4231) {
    // RFC 4231 test case 2.
    const std::string key = "Jefe";
    const std::string data = "what do ya want for nothing?";
    const Sha256 expected = {0x5b, 0xdc, 0xc1, 0x46, 0xbf, 0x60, 0x75, 0x4e, 0x6a, 0x04, 0x24,
                             0x26, 0x08, 0x95, 0x75, 0xc7, 0x5a, 0x00, 0x3f, 0x08, 0x9d, 0x27,
                             0x39, 0x83, 0x9d, 0xec, 0x58, 0xb9, 0x64, 0xec, 0x38, 0x43};
    EXPECT_EQ(hmacSha256(reinterpret_cast<const uint8_t*>(key.data()), key.size(),
                         reinterpret_cast<const uint8_t*>(data.data()), data.size()),
              expected);
}

// Vectors are computed by scripts/ota_tools/ap_pass.py with Python's hmac module.
TEST(OtaCrypto, ApCredentialsMatchThePythonTool) {
    DeploymentKey key;
    std::copy(AP_VECTOR_KEY, AP_VECTOR_KEY + key.size(), key.begin());
    for (const ApPassVector& vector : AP_PASS_VECTORS) {
        EXPECT_EQ(apSsid(vector.node), vector.ssid);
        EXPECT_EQ(apPassword(key, vector.node), vector.password) << vector.ssid;
    }
}

TEST(OtaCrypto, KeyFingerprintMatchesThePythonTool) {
    DeploymentKey key;
    std::copy(AP_VECTOR_KEY, AP_VECTOR_KEY + key.size(), key.begin());
    EXPECT_EQ(keyFingerprint(key), AP_VECTOR_KEY_FINGERPRINT);
}

TEST(OtaCrypto, ParsesADeploymentKeyInHex) {
    DeploymentKey key;
    std::string hex = "000102030405060708090a0b0c0d0e0f101112131415161718191A1B1C1D1E1F";
    ASSERT_TRUE(parseDeploymentKey(hex, key));
    EXPECT_TRUE(std::equal(key.begin(), key.end(), AP_VECTOR_KEY));
    EXPECT_TRUE(parseDeploymentKey("  " + hex + "\r\n", key));
}

TEST(OtaCrypto, RejectsMalformedDeploymentKeys) {
    DeploymentKey key{};
    std::string good(64, 'a');
    EXPECT_FALSE(parseDeploymentKey(good.substr(1), key));
    EXPECT_FALSE(parseDeploymentKey(good + "aa", key));
    EXPECT_FALSE(parseDeploymentKey(std::string(63, 'a') + "g", key));
    EXPECT_FALSE(parseDeploymentKey("", key));
    EXPECT_FALSE(parseDeploymentKey(std::string(64, '0'), key));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {
    }
    return 0;
}
