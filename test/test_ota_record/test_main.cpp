#include <gtest/gtest.h>

#include "otaRecord.h"

namespace {

ShaPrefix sha(uint8_t seed) {
    ShaPrefix out{};
    for (size_t i = 0; i < out.size(); i++) out[i] = static_cast<uint8_t>(seed + i);
    return out;
}

OtaRecord sampleRecord() {
    OtaRecord record;
    record.attemptSha = sha(0x10);
    record.pending = true;
    record.unexplainedRollbacks = 1;
    record.failReason = FailReason::MESH_SILENT;
    record.lastOutcome = OtaOutcome::ROLLED_BACK;
    record.skipMeshCheck = true;
    for (uint8_t i = 0; i < 6; i++) record.blacklist.add(sha(0x40 + i * 0x10));
    return record;
}

}  // namespace

TEST(Blacklist, AddsAndFinds) {
    Blacklist list;
    list.add(sha(1));
    EXPECT_TRUE(list.contains(sha(1)));
    EXPECT_FALSE(list.contains(sha(2)));
    EXPECT_EQ(list.size(), 1u);
}

TEST(Blacklist, IgnoresDuplicates) {
    Blacklist list;
    list.add(sha(1));
    list.add(sha(1));
    EXPECT_EQ(list.size(), 1u);
}

TEST(Blacklist, EvictsOldestWhenFull) {
    Blacklist list;
    for (uint8_t i = 0; i < 5; i++) list.add(sha(i * 0x10));
    EXPECT_EQ(list.size(), Blacklist::CAPACITY);
    EXPECT_FALSE(list.contains(sha(0x00)));
    for (uint8_t i = 1; i < 5; i++) EXPECT_TRUE(list.contains(sha(i * 0x10)));
    EXPECT_EQ(list.at(0), sha(0x10));
    EXPECT_EQ(list.at(3), sha(0x40));
}

TEST(OtaRecordCodec, EncodesFixedSize) {
    EXPECT_EQ(OtaRecordCodec::encode(OtaRecord{}).size(), OtaRecordCodec::SIZE);
}

TEST(OtaRecordCodec, RoundTrips) {
    OtaRecord in = sampleRecord();
    std::vector<uint8_t> blob = OtaRecordCodec::encode(in);
    OtaRecord out;
    ASSERT_TRUE(OtaRecordCodec::decode(blob.data(), blob.size(), out));
    EXPECT_EQ(out, in);
}

TEST(OtaRecordCodec, RoundTripsDefault) {
    std::vector<uint8_t> blob = OtaRecordCodec::encode(OtaRecord{});
    OtaRecord out = sampleRecord();
    ASSERT_TRUE(OtaRecordCodec::decode(blob.data(), blob.size(), out));
    EXPECT_EQ(out, OtaRecord{});
}

TEST(OtaRecordCodec, RejectsBadMagic) {
    std::vector<uint8_t> blob = OtaRecordCodec::encode(sampleRecord());
    ASSERT_EQ(blob.size(), OtaRecordCodec::SIZE);
    blob[0] ^= 0xFF;
    OtaRecord out;
    EXPECT_FALSE(OtaRecordCodec::decode(blob.data(), blob.size(), out));
}

TEST(OtaRecordCodec, RejectsTruncatedBlob) {
    std::vector<uint8_t> blob = OtaRecordCodec::encode(sampleRecord());
    ASSERT_EQ(blob.size(), OtaRecordCodec::SIZE);
    OtaRecord out;
    EXPECT_FALSE(OtaRecordCodec::decode(blob.data(), blob.size() - 1, out));
    EXPECT_FALSE(OtaRecordCodec::decode(blob.data(), 3, out));
    EXPECT_FALSE(OtaRecordCodec::decode(nullptr, 0, out));
}

TEST(OtaRecordCodec, RejectsCorruptBlacklistCount) {
    std::vector<uint8_t> blob = OtaRecordCodec::encode(sampleRecord());
    ASSERT_EQ(blob.size(), OtaRecordCodec::SIZE);
    blob[17] = Blacklist::CAPACITY + 1;
    OtaRecord out;
    EXPECT_FALSE(OtaRecordCodec::decode(blob.data(), blob.size(), out));
}

TEST(OtaRecordCodec, DecodesLongerRecordFromNewerImage) {
    OtaRecord in = sampleRecord();
    std::vector<uint8_t> blob = OtaRecordCodec::encode(in);
    ASSERT_EQ(blob.size(), OtaRecordCodec::SIZE);
    blob[2] = OtaRecordCodec::VERSION + 1;
    blob.insert(blob.end(), {0xAA, 0xBB, 0xCC});
    blob[3] = static_cast<uint8_t>(blob.size());
    OtaRecord out;
    ASSERT_TRUE(OtaRecordCodec::decode(blob.data(), blob.size(), out));
    EXPECT_EQ(out, in);
}

TEST(OtaRecordCodec, DecodesVersion1RecordOfOlderImage) {
    OtaRecord in = sampleRecord();
    std::vector<uint8_t> blob = OtaRecordCodec::encode(in);
    blob.resize(OtaRecordCodec::V1_SIZE);
    blob[2] = 1;
    blob[3] = static_cast<uint8_t>(OtaRecordCodec::V1_SIZE);
    OtaRecord out;
    ASSERT_TRUE(OtaRecordCodec::decode(blob.data(), blob.size(), out));
    in.skipMeshCheck = false;
    EXPECT_EQ(out, in);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (RUN_ALL_TESTS()) {
    }
    return 0;
}
