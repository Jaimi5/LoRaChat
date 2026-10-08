#include <gtest/gtest.h>

#include "otaPolicy.h"

namespace {

ShaPrefix prefix(uint8_t seed) {
    ShaPrefix out{};
    for (size_t i = 0; i < out.size(); i++) out[i] = static_cast<uint8_t>(seed + i);
    return out;
}

OtaManifest manifest() {
    OtaManifest m;
    m.keyId = 1;
    m.version = 0x00010300;  // 0.1.3
    m.imageSize = 1182800;
    m.boardEnv = "tbeam";
    m.project = "LoRaChat";
    m.versionString = "0.1.3+gabc1234";
    for (size_t i = 0; i < m.imageSha.size(); i++) m.imageSha[i] = static_cast<uint8_t>(0x30 + i);
    m.partitionTablePrefix = prefix(0xA0);
    return m;
}

NodeState node() {
    NodeState n;
    n.boardEnv = "tbeam";
    n.runningVersion = 0x00010200;  // 0.1.2
    n.partitionTablePrefix = prefix(0xA0);
    n.slotSize = 0x1C0000;
    n.batteryMv = 3900;
    n.externalPower = false;
    n.freeHeap = 150 * 1024;
    return n;
}

}  // namespace

TEST(OtaPolicy, InstallsANewerImageForThisBoard) {
    EXPECT_EQ(OtaPolicy::decide(manifest(), node()), PolicyDecision::INSTALL);
}

// Case 12
TEST(OtaPolicy, RefusesAnotherBoard) {
    OtaManifest m = manifest();
    m.boardEnv = "heltec";
    EXPECT_EQ(OtaPolicy::decide(m, node()), PolicyDecision::REFUSE_BOARD);
}

TEST(OtaPolicy, RefusesUnknownImageType) {
    OtaManifest m = manifest();
    m.type = static_cast<ImageType>(2);
    EXPECT_EQ(OtaPolicy::decide(m, node()), PolicyDecision::REFUSE_TYPE);
}

// Case 15
TEST(OtaPolicy, RefusesAnOlderVersion) {
    OtaManifest m = manifest();
    m.version = 0x00010100;
    EXPECT_EQ(OtaPolicy::decide(m, node()), PolicyDecision::REFUSE_DOWNGRADE);
}

TEST(OtaPolicy, InstallsAnOlderVersionWhenDowngradeIsAllowed) {
    OtaManifest m = manifest();
    m.version = 0x00010100;
    m.flags = OtaManifest::FLAG_ALLOW_DOWNGRADE;
    EXPECT_EQ(OtaPolicy::decide(m, node()), PolicyDecision::INSTALL);
}

// Case 16
TEST(OtaPolicy, SameVersionIsANoOp) {
    OtaManifest m = manifest();
    m.version = 0x00010200;
    EXPECT_EQ(OtaPolicy::decide(m, node()), PolicyDecision::NO_OP_SAME_VERSION);
}

// Case 17
TEST(OtaPolicy, SkipsABlacklistedImage) {
    NodeState n = node();
    n.blacklist.add(manifest().imageShaPrefix());
    EXPECT_EQ(OtaPolicy::decide(manifest(), n), PolicyDecision::SKIP_BLACKLISTED);
}

// Case 18
TEST(OtaPolicy, RefusesOnLowBatteryWithoutExternalPower) {
    NodeState n = node();
    n.batteryMv = 3599;
    EXPECT_EQ(OtaPolicy::decide(manifest(), n), PolicyDecision::REFUSE_BATTERY);
    n.externalPower = true;
    EXPECT_EQ(OtaPolicy::decide(manifest(), n), PolicyDecision::INSTALL);
}

// Case 19
TEST(OtaPolicy, RefusesOnLowHeap) {
    NodeState n = node();
    n.freeHeap = 60 * 1024 - 1;
    EXPECT_EQ(OtaPolicy::decide(manifest(), n), PolicyDecision::REFUSE_HEAP);
}

// Case 33
TEST(OtaPolicy, RefusesAnImageBuiltForAnotherPartitionTable) {
    NodeState n = node();
    n.partitionTablePrefix = prefix(0xB0);
    EXPECT_EQ(OtaPolicy::decide(manifest(), n), PolicyDecision::REFUSE_PARTITION_TABLE);
}

TEST(OtaPolicy, RefusesAnImageLargerThanTheSlot) {
    OtaManifest m = manifest();
    m.imageSize = 0x1C0001;
    EXPECT_EQ(OtaPolicy::decide(m, node()), PolicyDecision::REFUSE_SIZE);
}

TEST(OtaPolicy, ManifestReasonsComeBeforeBatteryAndHeap) {
    OtaManifest m = manifest();
    m.boardEnv = "heltec";
    NodeState n = node();
    n.batteryMv = 3000;
    n.freeHeap = 0;
    EXPECT_EQ(OtaPolicy::decide(m, n), PolicyDecision::REFUSE_BOARD);
}

// Case 13
TEST(OtaPolicy, DescriptorMustMatchTheManifest) {
    EXPECT_TRUE(OtaPolicy::descriptorMatches(manifest(), "LoRaChat", "0.1.3+gabc1234"));
    EXPECT_FALSE(OtaPolicy::descriptorMatches(manifest(), "Other", "0.1.3+gabc1234"));
    EXPECT_FALSE(OtaPolicy::descriptorMatches(manifest(), "LoRaChat", "0.1.4+gabc1234"));
}

TEST(OtaPolicy, ParsesVersions) {
    uint32_t v = 0;
    EXPECT_TRUE(OtaPolicy::parseVersion("0.1.3", v));
    EXPECT_EQ(v, 0x00010300u);
    EXPECT_TRUE(OtaPolicy::parseVersion("1.2.255+gabc1234.dirty", v));
    EXPECT_EQ(v, 0x0102FF00u);
    for (const char* bad : {"", "1.2", "1.2.3.4", "256.0.0", "v1.0.0", "1..3", "1.2.3-rc1",
                            "First-LoRaChat-157-g0115ff2-dir"}) {
        EXPECT_FALSE(OtaPolicy::parseVersion(bad, v)) << bad;
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {
    }
    return 0;
}
