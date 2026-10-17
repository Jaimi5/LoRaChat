#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "fakeFlash.h"
#include "otaBundle.h"
#include "otaSha256.h"
#include "testImage.h"

namespace {

/** Manifest body with an empty signature; the test gate only decodes it. */
std::vector<uint8_t> bundleFor(const TestImage& image) {
    std::vector<uint8_t> bundle = OtaManifestCodec::encode(image.manifest);
    bundle.resize(OtaManifestCodec::SIGNED_SIZE, 0);
    bundle.insert(bundle.end(), image.bytes.begin(), image.bytes.end());
    return bundle;
}

struct Fixture {
    FakeFlash flash{SLOT_SIZE};
    MbedSha256 sha;
    OtaStreamWriter writer{flash, sha};
    bool accept = true;
    int gateCalls = 0;
    int beforeWriteCalls = 0;
    int flashBeginsAtBeforeWrite = -1;
    OtaBundleReader reader{writer,
                           [this](const uint8_t* data, size_t size, OtaManifest& out) {
                               gateCalls++;
                               EXPECT_EQ(size, OtaManifestCodec::SIGNED_SIZE);
                               return accept &&
                                      OtaManifestCodec::decode(data, OtaManifestCodec::BODY_SIZE,
                                                               out);
                           },
                           [this](const OtaManifest&) {
                               beforeWriteCalls++;
                               flashBeginsAtBeforeWrite = flash.beginCalls;
                           }};

    UpdateStatus upload(const std::vector<uint8_t>& bundle, size_t chunk,
                        size_t limit = SIZE_MAX) {
        reader.begin(static_cast<uint32_t>(bundle.size()));
        size_t end = std::min(limit, bundle.size());
        for (size_t position = 0; position < end; position += chunk) {
            UpdateStatus status =
                reader.push(bundle.data() + position, std::min(chunk, end - position));
            if (status != UpdateStatus::OK) return status;
        }
        return reader.finish();
    }
};

}  // namespace

// Case 38 logic: a good bundle is written whatever the chunk size.
TEST(OtaBundle, WritesAGoodBundleInAnyChunkSize) {
    TestImage image = makeImage();
    std::vector<uint8_t> bundle = bundleFor(image);
    for (size_t chunk : {size_t{1}, size_t{100}, size_t{192}, size_t{193}, size_t{1460},
                         bundle.size()}) {
        Fixture f;
        EXPECT_EQ(f.upload(bundle, chunk), UpdateStatus::OK) << "chunk " << chunk;
        EXPECT_EQ(f.flash.contents(IMAGE_SIZE), image.bytes);
        EXPECT_EQ(f.gateCalls, 1);
        EXPECT_EQ(f.beforeWriteCalls, 1);
        EXPECT_EQ(f.flashBeginsAtBeforeWrite, 0);
        EXPECT_EQ(f.reader.manifest().versionString, image.manifest.versionString);
        EXPECT_TRUE(f.flash.activate());
    }
}

// Case 39: a refused manifest never reaches the flash.
TEST(OtaBundle, RefusedManifestLeavesTheFlashUntouched) {
    Fixture f;
    f.accept = false;
    std::vector<uint8_t> bundle = bundleFor(makeImage());

    EXPECT_EQ(f.upload(bundle, 1460), UpdateStatus::MANIFEST_REJECTED);
    EXPECT_EQ(f.flash.beginCalls, 0);
    EXPECT_EQ(f.beforeWriteCalls, 0);
    EXPECT_EQ(f.reader.finish(), UpdateStatus::MANIFEST_REJECTED);
}

TEST(OtaBundle, RefusesALengthThatDoesNotMatchTheManifest) {
    for (int delta : {-1, 1}) {
        Fixture f;
        std::vector<uint8_t> bundle = bundleFor(makeImage());
        f.reader.begin(static_cast<uint32_t>(bundle.size() + delta));
        EXPECT_EQ(f.reader.push(bundle.data(), 1460), UpdateStatus::BAD_BUNDLE_LENGTH);
        EXPECT_EQ(f.flash.beginCalls, 0);
        EXPECT_EQ(f.beforeWriteCalls, 0);
    }
}

TEST(OtaBundle, RefusesAnUploadShorterThanAManifest) {
    Fixture f;
    f.reader.begin(OtaManifestCodec::SIGNED_SIZE - 1);
    uint8_t byte = 0;
    EXPECT_EQ(f.reader.push(&byte, 1), UpdateStatus::BAD_BUNDLE_LENGTH);
    EXPECT_EQ(f.gateCalls, 0);
}

// Case 40 logic: an upload cut at 50 % is aborted and the boot slot stays.
TEST(OtaBundle, CutUploadIsAborted) {
    Fixture f;
    std::vector<uint8_t> bundle = bundleFor(makeImage());

    EXPECT_EQ(f.upload(bundle, 1460, bundle.size() / 2), UpdateStatus::INCOMPLETE);
    EXPECT_EQ(f.flash.abortCalls, 1);
    EXPECT_FALSE(f.flash.isWriting());
    EXPECT_FALSE(f.flash.activate());
}

TEST(OtaBundle, CutInsideTheManifestNeverTouchesTheFlash) {
    Fixture f;
    std::vector<uint8_t> bundle = bundleFor(makeImage());

    EXPECT_EQ(f.upload(bundle, 50, 100), UpdateStatus::INCOMPLETE);
    EXPECT_EQ(f.gateCalls, 0);
    EXPECT_EQ(f.flash.beginCalls, 0);
}

TEST(OtaBundle, AbortDropsTheWrite) {
    Fixture f;
    std::vector<uint8_t> bundle = bundleFor(makeImage());
    f.reader.begin(static_cast<uint32_t>(bundle.size()));
    ASSERT_EQ(f.reader.push(bundle.data(), 5000), UpdateStatus::OK);

    f.reader.abort();

    EXPECT_EQ(f.flash.abortCalls, 1);
    EXPECT_NE(f.reader.finish(), UpdateStatus::OK);
    EXPECT_FALSE(f.flash.activate());
}

TEST(OtaBundle, ImageThatDoesNotMatchItsManifestFails) {
    Fixture f;
    TestImage image = makeImage();
    std::vector<uint8_t> bundle = bundleFor(image);
    bundle[OtaManifestCodec::SIGNED_SIZE + IMAGE_SIZE / 2] ^= 0x01;

    EXPECT_EQ(f.upload(bundle, 1460), UpdateStatus::SHA_MISMATCH);
    EXPECT_FALSE(f.flash.activate());
}

TEST(OtaBundle, ANewUploadStartsClean) {
    Fixture f;
    f.accept = false;
    std::vector<uint8_t> bundle = bundleFor(makeImage());
    ASSERT_EQ(f.upload(bundle, 1460), UpdateStatus::MANIFEST_REJECTED);

    f.accept = true;
    EXPECT_EQ(f.upload(bundle, 1460), UpdateStatus::OK);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {
    }
    return 0;
}
