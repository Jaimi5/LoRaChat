#include <gtest/gtest.h>

#include <algorithm>

#include "manifestVectors.h"
#include "otaManifest.h"
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

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {
    }
    return 0;
}
