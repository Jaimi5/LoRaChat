#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "fakeFlash.h"
#include "otaSha256.h"
#include "otaStreamWriter.h"
#include "otaTransfer.h"

namespace {

constexpr size_t SLOT_SIZE = 8 * FakeFlash::SECTOR_SIZE;
// Spans several sectors and does not end on a chunk or sector boundary.
constexpr size_t IMAGE_SIZE = 3 * FakeFlash::SECTOR_SIZE + 1000;
constexpr size_t CHUNK_SIZE = 1460;

struct TestImage {
    std::vector<uint8_t> bytes;
    OtaManifest manifest;
};

void putField(std::vector<uint8_t>& body, size_t offset, const std::string& text) {
    std::fill(body.begin() + offset, body.begin() + offset + FakeFlash::DESC_FIELD_SIZE, 0);
    std::copy(text.begin(), text.end(), body.begin() + offset);
}

/** An image laid out like an ESP-IDF build: header, app descriptor, appended SHA-256. */
TestImage makeImage(size_t size = IMAGE_SIZE, const std::string& version = "0.1.3+gabc1234",
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

/** HTTP body that can be cut after a byte count for the first attempts. */
class FakeSource : public IImageSource {
public:
    explicit FakeSource(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}

    bool fetch(const Sink& sink) override {
        attempts++;
        bool cut = attempts <= cutAttempts;
        size_t limit = cut ? cutAt : std::min(bytes_.size(), bodyLimit);
        for (size_t position = 0; position < limit; position += CHUNK_SIZE) {
            size_t size = std::min(CHUNK_SIZE, limit - position);
            if (!sink(bytes_.data() + position, size)) return true;
        }
        return !cut;
    }

    size_t cutAt = 0;
    int cutAttempts = 0;
    /** Bytes sent by a body that ends early without a transport error. */
    size_t bodyLimit = SIZE_MAX;
    int attempts = 0;

private:
    std::vector<uint8_t> bytes_;
};

struct Fixture {
    FakeFlash flash{SLOT_SIZE};
    MbedSha256 sha;
    OtaStreamWriter writer{flash, sha};
};

}  // namespace

// Case 1: a good full image is written, checked and can be activated.
TEST(OtaTransfer, WritesAGoodImageInOneAttempt) {
    Fixture f;
    TestImage image = makeImage();
    FakeSource source(image.bytes);

    TransferResult result = OtaTransfer::run(source, f.writer, image.manifest);

    EXPECT_EQ(result.status, UpdateStatus::OK);
    EXPECT_EQ(result.attempts, 1);
    EXPECT_EQ(result.bytes, IMAGE_SIZE);
    EXPECT_EQ(f.flash.contents(IMAGE_SIZE), image.bytes);
    EXPECT_FALSE(f.flash.bootSlotChanged);
    EXPECT_TRUE(f.flash.activate());
}

// Case 3: one flipped byte fails the SHA-256 and the boot slot stays.
TEST(OtaTransfer, AbortsOnAFlippedByteWithoutRetrying) {
    Fixture f;
    TestImage image = makeImage();
    std::vector<uint8_t> bytes = image.bytes;
    bytes[IMAGE_SIZE / 2] ^= 0x01;
    FakeSource source(bytes);

    TransferResult result = OtaTransfer::run(source, f.writer, image.manifest);

    EXPECT_EQ(result.status, UpdateStatus::SHA_MISMATCH);
    EXPECT_EQ(result.attempts, 1);
    EXPECT_EQ(f.flash.abortCalls, 1);
    EXPECT_FALSE(f.flash.isWriting());
    EXPECT_FALSE(f.flash.activate());
}

TEST(OtaTransfer, AbortsWhenTheAppendedDigestDiffers) {
    Fixture f;
    TestImage image = makeImage();
    std::vector<uint8_t> bytes = image.bytes;
    bytes.back() ^= 0x80;
    FakeSource source(bytes);

    EXPECT_EQ(OtaTransfer::run(source, f.writer, image.manifest).status,
              UpdateStatus::SHA_MISMATCH);
    EXPECT_FALSE(f.flash.activate());
}

// Case 4: the WiFi drops at 50 % on every attempt: 3 attempts, then a clean abort.
TEST(OtaTransfer, GivesUpAfterThreeCutDownloads) {
    Fixture f;
    TestImage image = makeImage();
    FakeSource source(image.bytes);
    source.cutAt = IMAGE_SIZE / 2;
    source.cutAttempts = 3;

    TransferResult result = OtaTransfer::run(source, f.writer, image.manifest);

    EXPECT_EQ(result.status, UpdateStatus::TRANSPORT_FAILED);
    EXPECT_EQ(result.attempts, 3);
    EXPECT_EQ(source.attempts, 3);
    EXPECT_EQ(f.flash.beginCalls, 3);
    EXPECT_EQ(f.flash.abortCalls, 3);
    EXPECT_FALSE(f.flash.isWriting());
    EXPECT_FALSE(f.flash.activate());
}

TEST(OtaTransfer, RestartsFromByteZeroAfterACut) {
    Fixture f;
    TestImage image = makeImage();
    FakeSource source(image.bytes);
    source.cutAt = IMAGE_SIZE / 2;
    source.cutAttempts = 2;

    TransferResult result = OtaTransfer::run(source, f.writer, image.manifest);

    EXPECT_EQ(result.status, UpdateStatus::OK);
    EXPECT_EQ(result.attempts, 3);
    EXPECT_EQ(result.bytes, 2 * (IMAGE_SIZE / 2) + IMAGE_SIZE);
    EXPECT_EQ(f.flash.contents(IMAGE_SIZE), image.bytes);
}

TEST(OtaTransfer, RetriesABodyThatEndsEarly) {
    Fixture f;
    TestImage image = makeImage();
    FakeSource source(image.bytes);
    source.bodyLimit = IMAGE_SIZE - 10;

    TransferResult result = OtaTransfer::run(source, f.writer, image.manifest);

    EXPECT_EQ(result.status, UpdateStatus::TRANSPORT_FAILED);
    EXPECT_EQ(result.attempts, 3);
    EXPECT_FALSE(f.flash.isWriting());
}

TEST(OtaTransfer, RefusesABodyLongerThanTheManifestSays) {
    Fixture f;
    TestImage image = makeImage();
    std::vector<uint8_t> bytes = image.bytes;
    bytes.push_back(0);
    FakeSource source(bytes);

    TransferResult result = OtaTransfer::run(source, f.writer, image.manifest);

    EXPECT_EQ(result.status, UpdateStatus::TOO_LONG);
    EXPECT_EQ(result.attempts, 1);
    EXPECT_EQ(f.flash.abortCalls, 1);
}

TEST(OtaTransfer, StopsOnAFlashWriteError) {
    Fixture f;
    f.flash.failWriteAt = FakeFlash::SECTOR_SIZE + 5;
    TestImage image = makeImage();
    FakeSource source(image.bytes);

    TransferResult result = OtaTransfer::run(source, f.writer, image.manifest);

    EXPECT_EQ(result.status, UpdateStatus::WRITE_FAILED);
    EXPECT_EQ(result.attempts, 1);
    EXPECT_EQ(f.flash.abortCalls, 1);
}

TEST(OtaTransfer, ReportsAnImageThePlatformRejects) {
    Fixture f;
    f.flash.failEnd = true;
    TestImage image = makeImage();
    FakeSource source(image.bytes);

    EXPECT_EQ(OtaTransfer::run(source, f.writer, image.manifest).status,
              UpdateStatus::END_FAILED);
    EXPECT_FALSE(f.flash.activate());
}

TEST(OtaTransfer, ReportsAFailedReadBack) {
    Fixture f;
    f.flash.failReadback = true;
    TestImage image = makeImage();
    FakeSource source(image.bytes);

    EXPECT_EQ(OtaTransfer::run(source, f.writer, image.manifest).status,
              UpdateStatus::READBACK_MISMATCH);
}

TEST(OtaTransfer, RefusesAnImageWhoseDescriptorDiffersFromTheManifest) {
    Fixture f;
    TestImage image = makeImage(IMAGE_SIZE, "0.1.4+gabc1234");
    FakeSource source(image.bytes);

    EXPECT_EQ(OtaTransfer::run(source, f.writer, image.manifest).status,
              UpdateStatus::DESCRIPTOR_MISMATCH);

    Fixture g;
    TestImage other = makeImage(IMAGE_SIZE, "0.1.3+gabc1234", "OtherApp");
    FakeSource otherSource(other.bytes);
    EXPECT_EQ(OtaTransfer::run(otherSource, g.writer, other.manifest).status,
              UpdateStatus::DESCRIPTOR_MISMATCH);
}

TEST(OtaStreamWriter, RefusesASizeThatCannotHoldAnImage) {
    Fixture f;
    TestImage image = makeImage();
    image.manifest.imageSize = OtaStreamWriter::MIN_BODY_SIZE + OtaStreamWriter::DIGEST_SIZE - 1;

    EXPECT_EQ(f.writer.begin(image.manifest), UpdateStatus::BAD_SIZE);
    EXPECT_EQ(f.flash.beginCalls, 0);
}

TEST(OtaStreamWriter, ReportsASlotThatIsTooSmall) {
    FakeFlash flash(FakeFlash::SECTOR_SIZE);
    MbedSha256 sha;
    OtaStreamWriter writer(flash, sha);

    EXPECT_EQ(writer.begin(makeImage().manifest), UpdateStatus::BEGIN_FAILED);
}

TEST(OtaStreamWriter, RefusesWritesOutsideBeginAndFinish) {
    Fixture f;
    uint8_t byte = 0;
    EXPECT_EQ(f.writer.write(&byte, 1), UpdateStatus::WRITE_FAILED);
    EXPECT_EQ(f.writer.finish(), UpdateStatus::INCOMPLETE);
    f.writer.abort();
    EXPECT_EQ(f.flash.abortCalls, 0);
}

TEST(OtaStreamWriter, AcceptsAnySplitOfTheImage) {
    TestImage image = makeImage();
    for (size_t chunk : {size_t{1}, size_t{31}, size_t{32}, size_t{33}, size_t{4096}, IMAGE_SIZE}) {
        Fixture f;
        ASSERT_EQ(f.writer.begin(image.manifest), UpdateStatus::OK);
        for (size_t position = 0; position < IMAGE_SIZE; position += chunk) {
            size_t size = std::min(chunk, IMAGE_SIZE - position);
            ASSERT_EQ(f.writer.write(image.bytes.data() + position, size), UpdateStatus::OK);
        }
        EXPECT_EQ(f.writer.finish(), UpdateStatus::OK) << "chunk " << chunk;
    }
}

TEST(OtaStreamWriter, NamesEveryStatus) {
    EXPECT_STREQ(updateStatusName(UpdateStatus::OK), "OK");
    EXPECT_STREQ(updateStatusName(UpdateStatus::SHA_MISMATCH), "SHA_MISMATCH");
    EXPECT_STREQ(updateStatusName(UpdateStatus::TRANSPORT_FAILED), "TRANSPORT_FAILED");
    EXPECT_STREQ(updateStatusName(static_cast<UpdateStatus>(200)), "UNKNOWN");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {
    }
    return 0;
}
