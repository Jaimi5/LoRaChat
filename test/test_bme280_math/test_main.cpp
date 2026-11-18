#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include "bme280Math.h"

namespace {

/** Calibration of the worked example in the BMP280 datasheet (section 3.12). */
Bme280Calibration datasheetCalibration() {
    Bme280Calibration c;
    c.t1 = 27504;
    c.t2 = 26435;
    c.t3 = -1000;
    c.p1 = 36477;
    c.p2 = -10685;
    c.p3 = 3024;
    c.p4 = 2855;
    c.p5 = 140;
    c.p6 = -7;
    c.p7 = 15500;
    c.p8 = -14600;
    c.p9 = 6000;
    return c;
}

/** Calibration with the typical values of a BME280. */
Bme280Calibration bme280Calibration() {
    Bme280Calibration c;
    c.t1 = 28485;
    c.t2 = 26735;
    c.t3 = 50;
    c.p1 = 36738;
    c.p2 = -10635;
    c.p3 = 3024;
    c.p4 = 6980;
    c.p5 = -4;
    c.p6 = -7;
    c.p7 = 9900;
    c.p8 = -10230;
    c.p9 = 4285;
    c.h1 = 75;
    c.h2 = 370;
    c.h3 = 0;
    c.h4 = 313;
    c.h5 = 50;
    c.h6 = 30;
    return c;
}

void putU16(std::vector<uint8_t>& bytes, size_t at, uint16_t value) {
    bytes[at] = static_cast<uint8_t>(value & 0xFF);
    bytes[at + 1] = static_cast<uint8_t>(value >> 8);
}

/** @return the bytes a chip with calibration @p c gives from register 0x88. */
std::vector<uint8_t> tpBytes(const Bme280Calibration& c) {
    std::vector<uint8_t> bytes(BME280_CALIB_TP_SIZE, 0);
    putU16(bytes, 0, c.t1);
    putU16(bytes, 2, static_cast<uint16_t>(c.t2));
    putU16(bytes, 4, static_cast<uint16_t>(c.t3));
    putU16(bytes, 6, c.p1);
    const int16_t pressure[] = {c.p2, c.p3, c.p4, c.p5, c.p6, c.p7, c.p8, c.p9};
    for (size_t i = 0; i < 8; i++) putU16(bytes, 8 + 2 * i, static_cast<uint16_t>(pressure[i]));
    bytes[25] = c.h1;
    return bytes;
}

/** @return the bytes a chip with calibration @p c gives from register 0xE1. */
std::vector<uint8_t> hBytes(const Bme280Calibration& c) {
    std::vector<uint8_t> bytes(BME280_CALIB_H_SIZE, 0);
    putU16(bytes, 0, static_cast<uint16_t>(c.h2));
    bytes[2] = c.h3;
    uint16_t h4 = static_cast<uint16_t>(c.h4) & 0x0FFF;
    uint16_t h5 = static_cast<uint16_t>(c.h5) & 0x0FFF;
    bytes[3] = static_cast<uint8_t>(h4 >> 4);
    bytes[4] = static_cast<uint8_t>((h4 & 0x0F) | ((h5 & 0x0F) << 4));
    bytes[5] = static_cast<uint8_t>(h5 >> 4);
    bytes[6] = static_cast<uint8_t>(c.h6);
    return bytes;
}

/** @return the bytes at register 0xF7 for the given uncompensated values. */
std::vector<uint8_t> dataBytes(uint32_t pressure, uint32_t temperature, uint32_t humidity) {
    return {static_cast<uint8_t>(pressure >> 12),   static_cast<uint8_t>(pressure >> 4),
            static_cast<uint8_t>(pressure << 4),    static_cast<uint8_t>(temperature >> 12),
            static_cast<uint8_t>(temperature >> 4), static_cast<uint8_t>(temperature << 4),
            static_cast<uint8_t>(humidity >> 8),    static_cast<uint8_t>(humidity)};
}

/** The floating point humidity formula of the BME280 datasheet, in percent. */
double referenceHumidity(const Bme280Calibration& c, double tFine, double raw) {
    double h = tFine - 76800.0;
    h = (raw - (c.h4 * 64.0 + c.h5 / 16384.0 * h)) *
        (c.h2 / 65536.0 * (1.0 + c.h6 / 67108864.0 * h * (1.0 + c.h3 / 67108864.0 * h)));
    return h * (1.0 - c.h1 * h / 524288.0);
}

}  // namespace

TEST(Bme280Chip, KnownIds) {
    EXPECT_EQ(bme280ChipFromId(0x60), Bme280Chip::BME280);
    EXPECT_EQ(bme280ChipFromId(0x58), Bme280Chip::BMP280);
    EXPECT_EQ(bme280ChipFromId(0x56), Bme280Chip::BMP280);
    EXPECT_EQ(bme280ChipFromId(0x57), Bme280Chip::BMP280);
    for (int id : {0x00, 0xFF, 0x55, 0x59, 0x61, 0x50}) {
        EXPECT_EQ(bme280ChipFromId(static_cast<uint8_t>(id)), Bme280Chip::NONE) << id;
    }
    EXPECT_STREQ(bme280ChipName(Bme280Chip::BME280), "BME280");
    EXPECT_STREQ(bme280ChipName(Bme280Chip::BMP280), "BMP280");
    EXPECT_STREQ(bme280ChipName(Bme280Chip::NONE), "none");
}

TEST(Bme280Compensation, DatasheetExample) {
    Bme280Raw raw;
    raw.temperature = 519888;
    raw.pressure = 415148;
    Bme280Reading reading;
    ASSERT_TRUE(compensateBme280(datasheetCalibration(), raw, false, reading));
    // The datasheet gives 25.08 degrees Celsius and 100653.27 Pa.
    EXPECT_EQ(reading.tempCentiC, 2508);
    EXPECT_EQ(reading.pressurePa, 100653U);
    EXPECT_FALSE(reading.hasHumidity);
    EXPECT_EQ(reading.rhCentiPercent, 0U);
}

TEST(Bme280Compensation, HumidityFollowsTheFloatingPointFormula) {
    Bme280Calibration c = bme280Calibration();
    Bme280Raw raw;
    raw.temperature = 528000;
    raw.pressure = 330000;
    for (uint32_t humidity : {28000U, 30000U, 33000U, 36000U}) {
        raw.humidity = humidity;
        Bme280Reading reading;
        ASSERT_TRUE(compensateBme280(c, raw, true, reading));
        EXPECT_EQ(reading.tempCentiC, 2303);
        EXPECT_EQ(reading.pressurePa, 102856U);
        EXPECT_TRUE(reading.hasHumidity);
        EXPECT_NEAR(reading.rhCentiPercent / 100.0, referenceHumidity(c, 117894.0, humidity), 0.02)
            << humidity;
    }
    raw.humidity = 30000;
    Bme280Reading reading;
    ASSERT_TRUE(compensateBme280(c, raw, true, reading));
    EXPECT_EQ(reading.rhCentiPercent, 5613U);
}

TEST(Bme280Compensation, HumidityIsLimitedToZeroAndOneHundred) {
    Bme280Raw raw;
    raw.temperature = 528000;
    raw.pressure = 330000;
    Bme280Reading reading;
    for (uint32_t humidity : {0U, 20000U}) {
        raw.humidity = humidity;
        ASSERT_TRUE(compensateBme280(bme280Calibration(), raw, true, reading));
        EXPECT_EQ(reading.rhCentiPercent, 0U);
    }
    for (uint32_t humidity : {45000U, 65535U}) {
        raw.humidity = humidity;
        ASSERT_TRUE(compensateBme280(bme280Calibration(), raw, true, reading));
        EXPECT_EQ(reading.rhCentiPercent, 10000U);
    }
}

TEST(Bme280Compensation, ColdAndHot) {
    Bme280Raw raw;
    raw.humidity = 30000;
    raw.temperature = 400000;
    raw.pressure = 330000;
    Bme280Reading reading;
    ASSERT_TRUE(compensateBme280(bme280Calibration(), raw, true, reading));
    EXPECT_EQ(reading.tempCentiC, -1777);
    EXPECT_EQ(reading.pressurePa, 96243U);
    EXPECT_NEAR(reading.rhCentiPercent / 100.0, 54.30, 0.02);

    raw.temperature = 620000;
    raw.pressure = 250000;
    ASSERT_TRUE(compensateBme280(bme280Calibration(), raw, true, reading));
    EXPECT_EQ(reading.tempCentiC, 5236);
    EXPECT_EQ(reading.pressurePa, 122055U);
    EXPECT_NEAR(reading.rhCentiPercent / 100.0, 57.04, 0.02);
}

TEST(Bme280Compensation, RefusesACalibrationThatDividesByZero) {
    Bme280Calibration c = datasheetCalibration();
    c.p1 = 0;
    Bme280Raw raw;
    raw.temperature = 519888;
    raw.pressure = 415148;
    Bme280Reading reading;
    reading.tempCentiC = 77;
    EXPECT_FALSE(compensateBme280(c, raw, false, reading));
    EXPECT_EQ(reading.tempCentiC, 77);
}

TEST(Bme280Compensation, AnyInputGivesADefinedResult) {
    std::mt19937 random(4242);
    for (int i = 0; i < 20000; i++) {
        std::vector<uint8_t> tp(BME280_CALIB_TP_SIZE);
        std::vector<uint8_t> h(BME280_CALIB_H_SIZE);
        for (auto& byte : tp) byte = static_cast<uint8_t>(random());
        for (auto& byte : h) byte = static_cast<uint8_t>(random());
        Bme280Calibration c;
        if (!parseBme280Calibration(tp.data(), h.data(), c)) continue;
        Bme280Raw raw;
        raw.temperature = random() & 0xFFFFF;
        raw.pressure = random() & 0xFFFFF;
        raw.humidity = random() & 0xFFFF;
        Bme280Reading reading;
        if (!compensateBme280(c, raw, true, reading)) continue;
        EXPECT_LE(reading.rhCentiPercent, 10000U);
        MonSensor sensor = bme280ToMonSensor(reading);
        EXPECT_NE(sensor.rhCentiPercent, MON_NO_HUMIDITY);
    }
}

TEST(Bme280Calibration, ParsesTheBytesOfTheChip) {
    Bme280Calibration expected = bme280Calibration();
    std::vector<uint8_t> tp = tpBytes(expected);
    std::vector<uint8_t> h = hBytes(expected);
    Bme280Calibration c;
    ASSERT_TRUE(parseBme280Calibration(tp.data(), h.data(), c));
    EXPECT_EQ(c.t1, 28485);
    EXPECT_EQ(c.t2, 26735);
    EXPECT_EQ(c.t3, 50);
    EXPECT_EQ(c.p1, 36738);
    EXPECT_EQ(c.p2, -10635);
    EXPECT_EQ(c.p5, -4);
    EXPECT_EQ(c.p9, 4285);
    EXPECT_EQ(c.h1, 75);
    EXPECT_EQ(c.h2, 370);
    EXPECT_EQ(c.h3, 0);
    EXPECT_EQ(c.h4, 313);
    EXPECT_EQ(c.h5, 50);
    EXPECT_EQ(c.h6, 30);
}

TEST(Bme280Calibration, TwelveBitFieldsShareAByteAndCarryASign) {
    std::vector<uint8_t> tp = tpBytes(bme280Calibration());
    // 0xE4 = 0x12, 0xE5 = 0xA3, 0xE6 = 0xFE: dig_H4 = 0x123, dig_H5 = 0xFEA = -22.
    std::vector<uint8_t> h = {0x72, 0x01, 0x00, 0x12, 0xA3, 0xFE, 0xE2};
    Bme280Calibration c;
    ASSERT_TRUE(parseBme280Calibration(tp.data(), h.data(), c));
    EXPECT_EQ(c.h4, 0x123);
    EXPECT_EQ(c.h5, -22);
    EXPECT_EQ(c.h6, -30);
    h[3] = 0x80;
    h[4] = 0x00;
    ASSERT_TRUE(parseBme280Calibration(tp.data(), h.data(), c));
    EXPECT_EQ(c.h4, -2048);
}

TEST(Bme280Calibration, Bmp280HasNoHumidityPart) {
    std::vector<uint8_t> tp = tpBytes(datasheetCalibration());
    tp[25] = 0x4B;
    Bme280Calibration c;
    ASSERT_TRUE(parseBme280Calibration(tp.data(), nullptr, c));
    EXPECT_EQ(c.t1, 27504);
    EXPECT_EQ(c.p8, -14600);
    EXPECT_EQ(c.h1, 0);
    EXPECT_EQ(c.h2, 0);
}

TEST(Bme280Calibration, RefusesAnErasedOrAbsentChip) {
    Bme280Calibration c;
    c.t2 = 5;
    std::vector<uint8_t> zeros(BME280_CALIB_TP_SIZE, 0x00);
    std::vector<uint8_t> ones(BME280_CALIB_TP_SIZE, 0xFF);
    EXPECT_FALSE(parseBme280Calibration(zeros.data(), nullptr, c));
    EXPECT_FALSE(parseBme280Calibration(ones.data(), nullptr, c));
    std::vector<uint8_t> noPressure = tpBytes(datasheetCalibration());
    noPressure[6] = 0;
    noPressure[7] = 0;
    EXPECT_FALSE(parseBme280Calibration(noPressure.data(), nullptr, c));
    EXPECT_EQ(c.t2, 5);
}

TEST(Bme280Raw, ParsesTwentyBitAndSixteenBitValues) {
    std::vector<uint8_t> data = dataBytes(415148, 519888, 30000);
    Bme280Raw raw;
    ASSERT_TRUE(parseBme280Raw(data.data(), BME280_DATA_SIZE, raw));
    EXPECT_EQ(raw.pressure, 415148U);
    EXPECT_EQ(raw.temperature, 519888U);
    EXPECT_EQ(raw.humidity, 30000U);

    Bme280Raw short6;
    ASSERT_TRUE(parseBme280Raw(data.data(), BMP280_DATA_SIZE, short6));
    EXPECT_EQ(short6.pressure, 415148U);
    EXPECT_EQ(short6.temperature, 519888U);
    EXPECT_EQ(short6.humidity, 0U);

    std::vector<uint8_t> top = dataBytes(0xFFFFF, 0xFFFFF, 0xFFFF);
    ASSERT_TRUE(parseBme280Raw(top.data(), BME280_DATA_SIZE, raw));
    EXPECT_EQ(raw.pressure, 0xFFFFFU);
    EXPECT_EQ(raw.temperature, 0xFFFFFU);
    EXPECT_EQ(raw.humidity, 0xFFFFU);
}

TEST(Bme280Raw, RefusesASkippedMeasurementAndAWrongSize) {
    Bme280Raw raw;
    raw.pressure = 9;
    // The reset value of the data registers: nothing was measured.
    std::vector<uint8_t> skipped = {0x80, 0x00, 0x00, 0x80, 0x00, 0x00, 0x80, 0x00};
    EXPECT_FALSE(parseBme280Raw(skipped.data(), BME280_DATA_SIZE, raw));
    std::vector<uint8_t> noTemperature = dataBytes(415148, 0x80000, 30000);
    EXPECT_FALSE(parseBme280Raw(noTemperature.data(), BME280_DATA_SIZE, raw));
    std::vector<uint8_t> good = dataBytes(415148, 519888, 30000);
    EXPECT_FALSE(parseBme280Raw(good.data(), 7, raw));
    EXPECT_FALSE(parseBme280Raw(good.data(), 0, raw));
    EXPECT_EQ(raw.pressure, 9U);
}

TEST(Bme280Timing, MeasurementTimeCoversTheDatasheetMaximum) {
    EXPECT_GE(bme280MeasureTimeMs(true), 10U);
    EXPECT_GE(bme280MeasureTimeMs(false), 7U);
    EXPECT_LE(bme280MeasureTimeMs(true), 20U);
}

TEST(Bme280MonSensor, ConvertsTheUnits) {
    Bme280Reading reading;
    reading.tempCentiC = 2143;
    reading.pressurePa = 101325;
    reading.hasHumidity = true;
    reading.rhCentiPercent = 6100;
    MonSensor sensor = bme280ToMonSensor(reading);
    EXPECT_EQ(sensor.tempCentiC, 2143);
    EXPECT_EQ(sensor.rhCentiPercent, 6100);
    EXPECT_EQ(sensor.pressureDeciHpa, 10133);

    reading.hasHumidity = false;
    EXPECT_EQ(bme280ToMonSensor(reading).rhCentiPercent, MON_NO_HUMIDITY);
}

TEST(Bme280MonSensor, LimitsEachField) {
    Bme280Reading reading;
    reading.tempCentiC = -100000;
    reading.pressurePa = 0xFFFFFFFF;
    reading.hasHumidity = true;
    reading.rhCentiPercent = 0xFFFFFFFF;
    MonSensor sensor = bme280ToMonSensor(reading);
    EXPECT_EQ(sensor.tempCentiC, -32768);
    EXPECT_EQ(sensor.pressureDeciHpa, 0xFFFF);
    EXPECT_EQ(sensor.rhCentiPercent, 0xFFFE);
    reading.tempCentiC = 100000;
    EXPECT_EQ(bme280ToMonSensor(reading).tempCentiC, 32767);
}

TEST(Bme280MonSensor, SurvivesTheReportCodec) {
    Bme280Reading reading;
    reading.tempCentiC = -512;
    reading.pressurePa = 95432;
    MonReport report;
    report.hasSensor = true;
    report.sensor = bme280ToMonSensor(reading);
    std::vector<uint8_t> payload = encodeMonReport(report);
    MonReport decoded;
    ASSERT_TRUE(decodeMonReport(payload.data(), payload.size(), decoded));
    ASSERT_TRUE(decoded.hasSensor);
    EXPECT_EQ(decoded.sensor.tempCentiC, -512);
    EXPECT_EQ(decoded.sensor.rhCentiPercent, MON_NO_HUMIDITY);
    EXPECT_EQ(decoded.sensor.pressureDeciHpa, 9543);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
