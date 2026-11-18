#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "bme280Math.h"

/**
 * @brief Bosch BME280 or BMP280 on the board's I2C bus, at address 0x76 or 0x77.
 *
 * The chip sleeps between measurements: each call of measure() starts one forced conversion
 * at oversampling x1 and reads it. Any task may call any method after begin(). A board
 * without the sensor answers false everywhere. Without SENSOR_BME280_ENABLED the bus is
 * never touched.
 */
class Bme280Sensor {
public:
    static Bme280Sensor& getInstance() {
        static Bme280Sensor instance;
        return instance;
    }

    /** Looks for the chip once and reads its calibration. Call it after the bus has started. */
    void begin();

    bool present() const { return chip_ != Bme280Chip::NONE; }

    /**
     * @brief Takes one measurement. It blocks for the conversion, about 10 ms, and for no
     *        more than a few bus timeouts when the chip stops answering.
     * @return false if there is no sensor or the measurement failed.
     */
    bool measure(Bme280Reading& out);

    /**
     * @param ageSeconds time since the reading was taken.
     * @return false if no measurement has succeeded since boot.
     */
    bool lastReading(Bme280Reading& out, uint32_t& ageSeconds) const;

    /** Takes a measurement. @return chip, address and values as one line for /sensor. */
    std::string describe();

private:
    Bme280Sensor() = default;

    bool probe(uint8_t address);
    bool writeRegister(uint8_t reg, uint8_t value);
    bool readRegisters(uint8_t reg, uint8_t* out, size_t size);
    /** Waits until the status register clears @p bit, for at most @p tries polls. */
    bool waitStatusClear(uint8_t bit, uint8_t tries);
    bool measureLocked(Bme280Reading& out);

    /** Keeps the steps of one measurement together. */
    mutable std::mutex mutex_;
    std::atomic<Bme280Chip> chip_{Bme280Chip::NONE};
    uint8_t address_ = 0;
    Bme280Calibration calibration_;
    Bme280Reading last_;
    /** esp_timer time of last_, 0 while there is none. */
    int64_t lastAtUs_ = 0;
    /** The last measurement failed. Failures are logged once until one succeeds. */
    bool failing_ = false;
};
