#pragma once

#include <cstddef>
#include <cstdint>

#include "monReport.h"

/** Bosch environment sensors that share the BME280 register map. */
enum class Bme280Chip : uint8_t {
    NONE,
    /** Temperature and pressure. */
    BMP280,
    /** Temperature, pressure and humidity. */
    BME280,
};

/** Registers and commands of the BME280 and the BMP280. */
constexpr uint8_t BME280_REG_CALIB_TP = 0x88;
constexpr uint8_t BME280_REG_CHIP_ID = 0xD0;
constexpr uint8_t BME280_REG_RESET = 0xE0;
constexpr uint8_t BME280_REG_CALIB_H = 0xE1;
constexpr uint8_t BME280_REG_CTRL_HUM = 0xF2;
constexpr uint8_t BME280_REG_STATUS = 0xF3;
constexpr uint8_t BME280_REG_CTRL_MEAS = 0xF4;
constexpr uint8_t BME280_REG_CONFIG = 0xF5;
constexpr uint8_t BME280_REG_DATA = 0xF7;
constexpr uint8_t BME280_RESET_COMMAND = 0xB6;
/** Status bits: a conversion is running, the calibration is being copied from the NVM. */
constexpr uint8_t BME280_STATUS_MEASURING = 1U << 3;
constexpr uint8_t BME280_STATUS_IM_UPDATE = 1U << 0;
/** ctrl_hum: humidity oversampling x1. */
constexpr uint8_t BME280_CTRL_HUM_X1 = 0x01;
/** ctrl_meas: temperature and pressure oversampling x1, sleep mode. */
constexpr uint8_t BME280_CTRL_MEAS_SLEEP = 0x24;
/** ctrl_meas: temperature and pressure oversampling x1, one forced measurement. */
constexpr uint8_t BME280_CTRL_MEAS_FORCED = 0x25;

/** Bytes from BME280_REG_CALIB_TP: dig_T1 to dig_P9, one reserved byte and dig_H1. */
constexpr size_t BME280_CALIB_TP_SIZE = 26;
/** Bytes from BME280_REG_CALIB_H: dig_H2 to dig_H6. */
constexpr size_t BME280_CALIB_H_SIZE = 7;
/** Bytes from BME280_REG_DATA: pressure (3), temperature (3) and humidity (2). */
constexpr size_t BME280_DATA_SIZE = 8;
/** The same without the humidity, as the BMP280 gives it. */
constexpr size_t BMP280_DATA_SIZE = 6;

/** Factory calibration of one chip. */
struct Bme280Calibration {
    uint16_t t1 = 0;
    int16_t t2 = 0;
    int16_t t3 = 0;
    uint16_t p1 = 0;
    int16_t p2 = 0;
    int16_t p3 = 0;
    int16_t p4 = 0;
    int16_t p5 = 0;
    int16_t p6 = 0;
    int16_t p7 = 0;
    int16_t p8 = 0;
    int16_t p9 = 0;
    uint8_t h1 = 0;
    int16_t h2 = 0;
    uint8_t h3 = 0;
    int16_t h4 = 0;
    int16_t h5 = 0;
    int8_t h6 = 0;
};

/** Uncompensated output of one measurement. */
struct Bme280Raw {
    uint32_t pressure = 0;
    uint32_t temperature = 0;
    uint32_t humidity = 0;
};

/** One compensated measurement. */
struct Bme280Reading {
    /** Temperature in 0.01 degrees Celsius. */
    int32_t tempCentiC = 0;
    /** Pressure in Pa. */
    uint32_t pressurePa = 0;
    bool hasHumidity = false;
    /** Relative humidity in 0.01 %. */
    uint32_t rhCentiPercent = 0;
};

/** @return the chip that answers register BME280_REG_CHIP_ID with @p id. */
Bme280Chip bme280ChipFromId(uint8_t id);

/** @return "BME280", "BMP280" or "none". */
const char* bme280ChipName(Bme280Chip chip);

/**
 * @brief Reads the calibration from the bytes of the chip.
 * @param tp BME280_CALIB_TP_SIZE bytes read from BME280_REG_CALIB_TP.
 * @param h BME280_CALIB_H_SIZE bytes read from BME280_REG_CALIB_H, or nullptr on a BMP280.
 * @return false if the bytes are no calibration: an erased or absent chip reads all 0x00 or
 *         all 0xFF.
 */
bool parseBme280Calibration(const uint8_t* tp, const uint8_t* h, Bme280Calibration& out);

/**
 * @brief Reads the measurement from the bytes at BME280_REG_DATA.
 * @param size BME280_DATA_SIZE with humidity, BMP280_DATA_SIZE without.
 * @return false if @p size is neither, or the chip skipped the temperature or the pressure.
 */
bool parseBme280Raw(const uint8_t* data, size_t size, Bme280Raw& out);

/**
 * @brief Applies the integer compensation formulas of the Bosch datasheets.
 * @param withHumidity the chip is a BME280 and @p raw holds its humidity.
 * @return false if the calibration cannot give a pressure.
 */
bool compensateBme280(const Bme280Calibration& calibration, const Bme280Raw& raw, bool withHumidity,
                      Bme280Reading& out);

/**
 * @return the longest time one forced measurement at oversampling x1 takes, in ms, rounded up
 *         (datasheet, appendix B).
 */
uint32_t bme280MeasureTimeMs(bool withHumidity);

/** @return @p reading as the sensor block of a monitor report, each value limited to its field. */
MonSensor bme280ToMonSensor(const Bme280Reading& reading);
