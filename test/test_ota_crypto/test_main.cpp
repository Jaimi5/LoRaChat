#include <gtest/gtest.h>

#include <algorithm>

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

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {
    }
    return 0;
}
