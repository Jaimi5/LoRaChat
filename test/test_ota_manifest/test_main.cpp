#include <gtest/gtest.h>

#include <algorithm>

#include "otaManifest.h"

namespace {

OtaManifest sampleManifest() {
    OtaManifest m;
    m.flags = OtaManifest::FLAG_SKIP_MESH_CHECK;
    m.keyId = 0x00000001;
    m.version = 0x00010200;
    m.imageSize = 1182800;
    m.boardEnv = "tbeam";
    m.project = "LoRaChat";
    m.versionString = "0.1.2+gabc1234";
    for (size_t i = 0; i < m.imageSha.size(); i++) m.imageSha[i] = static_cast<uint8_t>(i * 7);
    for (size_t i = 0; i < m.partitionTablePrefix.size(); i++)
        m.partitionTablePrefix[i] = static_cast<uint8_t>(0xA0 + i);
    return m;
}

PublicKey keyWithSeed(uint8_t seed) {
    PublicKey key{};
    key[0] = 0x04;
    for (size_t i = 1; i < key.size(); i++) key[i] = static_cast<uint8_t>(seed + i);
    return key;
}

// Accepts a signature whose first byte equals the first byte of the key's X coordinate.
bool fakeVerify(const PublicKey& key, const uint8_t*, size_t size, const Signature& signature) {
    return size == OtaManifestCodec::BODY_SIZE && signature[0] == key[1];
}

std::vector<uint8_t> signedWith(const OtaManifest& m, const PublicKey& key) {
    std::vector<uint8_t> data = OtaManifestCodec::encode(m);
    Signature signature{};
    signature[0] = key[1];
    data.insert(data.end(), signature.begin(), signature.end());
    return data;
}

const std::vector<TrustedKey> KEYS = {
    {0x00000001, keyWithSeed(1), true},
    {0x00000100, keyWithSeed(9), false},
};

}  // namespace

TEST(OtaManifestCodec, BodyIs128Bytes) {
    EXPECT_EQ(OtaManifestCodec::encode(sampleManifest()).size(), 128u);
}

TEST(OtaManifestCodec, RoundTrips) {
    std::vector<uint8_t> body = OtaManifestCodec::encode(sampleManifest());
    OtaManifest decoded;
    ASSERT_TRUE(OtaManifestCodec::decode(body.data(), body.size(), decoded));
    EXPECT_EQ(decoded, sampleManifest());
    EXPECT_TRUE(decoded.skipMeshCheck());
    EXPECT_FALSE(decoded.allowDowngrade());
}

TEST(OtaManifestCodec, LayoutStartsWithMagicAndFormat) {
    std::vector<uint8_t> body = OtaManifestCodec::encode(sampleManifest());
    EXPECT_EQ(std::string(body.begin(), body.begin() + 4), "LMOT");
    EXPECT_EQ(body[4], OtaManifestCodec::FORMAT_VERSION);
    EXPECT_EQ(body[5], static_cast<uint8_t>(ImageType::FULL_APP));
}

TEST(OtaManifestCodec, RejectsStringsThatDoNotFit) {
    OtaManifest m = sampleManifest();
    m.boardEnv = std::string(OtaManifestCodec::BOARD_ENV_SIZE, 'x');
    EXPECT_TRUE(OtaManifestCodec::encode(m).empty());
    m = sampleManifest();
    m.versionString = std::string(OtaManifestCodec::VERSION_STRING_SIZE, '1');
    EXPECT_TRUE(OtaManifestCodec::encode(m).empty());
}

TEST(OtaManifestCodec, RejectsBadMagicFormatOrSize) {
    std::vector<uint8_t> body = OtaManifestCodec::encode(sampleManifest());
    OtaManifest out;
    EXPECT_FALSE(OtaManifestCodec::decode(body.data(), body.size() - 1, out));
    std::vector<uint8_t> badMagic = body;
    badMagic[0] = 'X';
    EXPECT_FALSE(OtaManifestCodec::decode(badMagic.data(), badMagic.size(), out));
    std::vector<uint8_t> badFormat = body;
    badFormat[4] = 2;
    EXPECT_FALSE(OtaManifestCodec::decode(badFormat.data(), badFormat.size(), out));
}

TEST(OtaManifestCodec, RejectsUnterminatedString) {
    std::vector<uint8_t> body = OtaManifestCodec::encode(sampleManifest());
    std::fill(body.begin() + 24, body.begin() + 40, 'x');  // board_env without a zero
    OtaManifest out;
    EXPECT_FALSE(OtaManifestCodec::decode(body.data(), body.size(), out));
}

TEST(VerifySignedManifest, AcceptsSignatureOfAKnownKey) {
    std::vector<uint8_t> data = signedWith(sampleManifest(), KEYS[0].publicKey);
    OtaManifest out;
    EXPECT_EQ(verifySignedManifest(data.data(), data.size(), KEYS, true, fakeVerify, out),
              ManifestCheck::OK);
    EXPECT_EQ(out, sampleManifest());
}

TEST(VerifySignedManifest, RejectsShortData) {
    std::vector<uint8_t> data = signedWith(sampleManifest(), KEYS[0].publicKey);
    OtaManifest out;
    EXPECT_EQ(verifySignedManifest(data.data(), 191, KEYS, true, fakeVerify, out),
              ManifestCheck::TOO_SHORT);
}

TEST(VerifySignedManifest, RejectsUnknownKeyId) {
    OtaManifest m = sampleManifest();
    m.keyId = 0x00000042;
    std::vector<uint8_t> data = signedWith(m, KEYS[0].publicKey);
    OtaManifest out;
    EXPECT_EQ(verifySignedManifest(data.data(), data.size(), KEYS, true, fakeVerify, out),
              ManifestCheck::UNKNOWN_KEY);
}

TEST(VerifySignedManifest, ProductionRefusesTestKey) {
    std::vector<uint8_t> data = signedWith(sampleManifest(), KEYS[0].publicKey);
    OtaManifest out;
    EXPECT_EQ(verifySignedManifest(data.data(), data.size(), KEYS, false, fakeVerify, out),
              ManifestCheck::TEST_KEY_REFUSED);
}

TEST(VerifySignedManifest, RejectsSignatureOfAnotherKey) {
    std::vector<uint8_t> data = signedWith(sampleManifest(), KEYS[1].publicKey);
    OtaManifest out;
    EXPECT_EQ(verifySignedManifest(data.data(), data.size(), KEYS, true, fakeVerify, out),
              ManifestCheck::BAD_SIGNATURE);
}

TEST(VerifySignedManifest, RejectsUnsignedManifest) {
    std::vector<uint8_t> data = OtaManifestCodec::encode(sampleManifest());
    data.resize(OtaManifestCodec::SIGNED_SIZE, 0);
    OtaManifest out;
    EXPECT_EQ(verifySignedManifest(data.data(), data.size(), KEYS, true, fakeVerify, out),
              ManifestCheck::BAD_SIGNATURE);
}

TEST(VerifySignedManifest, LeavesOutputUntouchedOnFailure) {
    std::vector<uint8_t> data = signedWith(sampleManifest(), KEYS[1].publicKey);
    OtaManifest out;
    out.boardEnv = "unchanged";
    verifySignedManifest(data.data(), data.size(), KEYS, true, fakeVerify, out);
    EXPECT_EQ(out.boardEnv, "unchanged");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {
    }
    return 0;
}
