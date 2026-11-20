#include "bme280.h"

#include <Arduino.h>
#include <Wire.h>

#include <algorithm>
#include <cstdlib>

#include "config.h"
#include "devices/initDevices.h"
#include "esp_timer.h"

#ifdef SENSOR_BME280_ENABLED

static const char* BME_TAG = "Bme280";
static constexpr uint8_t ADDRESSES[] = {0x76, 0x77};
/** Time the chip needs after power-on or a reset before it answers. */
static constexpr uint32_t STARTUP_MS = 3;
static constexpr uint32_t POLL_MS = 2;
static constexpr uint8_t POLL_TRIES = 5;
static constexpr int64_t US_PER_S = 1000000;

bool Bme280Sensor::writeRegister(uint8_t reg, uint8_t value) {
    std::lock_guard<std::mutex> bus(InitDevices::i2cMutex());
    Wire.beginTransmission(address_);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool Bme280Sensor::readRegisters(uint8_t reg, uint8_t* out, size_t size) {
    // The bus mutex covers the register address, the read and the copy out of the shared
    // receive buffer of Wire.
    std::lock_guard<std::mutex> bus(InitDevices::i2cMutex());
    Wire.beginTransmission(address_);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    size_t received = Wire.requestFrom(address_, size, true);
    if (received != size) {
        while (Wire.available() > 0) Wire.read();
        return false;
    }
    for (size_t i = 0; i < size; i++) out[i] = static_cast<uint8_t>(Wire.read());
    return true;
}

bool Bme280Sensor::waitStatusClear(uint8_t bit, uint8_t tries) {
    for (uint8_t attempt = 0; attempt < tries; attempt++) {
        uint8_t status = 0;
        if (!readRegisters(BME280_REG_STATUS, &status, 1)) return false;
        if ((status & bit) == 0) return true;
        delay(POLL_MS);
    }
    return false;
}

bool Bme280Sensor::probe(uint8_t address) {
    address_ = address;
    uint8_t id = 0;
    if (!readRegisters(BME280_REG_CHIP_ID, &id, 1)) return false;
    Bme280Chip chip = bme280ChipFromId(id);
    if (chip == Bme280Chip::NONE) {
        ESP_LOGW(BME_TAG, "Unknown chip id 0x%02X at I2C address 0x%02X", id, address);
        return false;
    }

    // The reset leaves the chip in sleep mode with the filter off.
    if (!writeRegister(BME280_REG_RESET, BME280_RESET_COMMAND)) return false;
    delay(STARTUP_MS);
    if (!waitStatusClear(BME280_STATUS_IM_UPDATE, POLL_TRIES)) return false;

    uint8_t tp[BME280_CALIB_TP_SIZE];
    uint8_t h[BME280_CALIB_H_SIZE];
    bool humidity = chip == Bme280Chip::BME280;
    if (!readRegisters(BME280_REG_CALIB_TP, tp, sizeof(tp))) return false;
    if (humidity && !readRegisters(BME280_REG_CALIB_H, h, sizeof(h))) return false;
    if (!parseBme280Calibration(tp, humidity ? h : nullptr, calibration_)) {
        ESP_LOGW(BME_TAG, "%s at 0x%02X has no calibration", bme280ChipName(chip), address);
        return false;
    }
    chip_ = chip;
    return true;
}

void Bme280Sensor::begin() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (present()) return;
    for (uint8_t address : ADDRESSES) {
        if (probe(address)) {
            ESP_LOGI(BME_TAG, "%s found at I2C address 0x%02X", bme280ChipName(chip_), address);
            return;
        }
    }
    address_ = 0;
    ESP_LOGI(BME_TAG, "No BME280 or BMP280 at I2C address 0x76 or 0x77");
}

bool Bme280Sensor::measureLocked(Bme280Reading& out) {
    bool humidity = chip_ == Bme280Chip::BME280;
    // The humidity setting only takes effect with the write of ctrl_meas that follows it.
    if (humidity && !writeRegister(BME280_REG_CTRL_HUM, BME280_CTRL_HUM_X1)) return false;
    if (!writeRegister(BME280_REG_CTRL_MEAS, BME280_CTRL_MEAS_FORCED)) return false;
    delay(bme280MeasureTimeMs(humidity));
    // The chip returns to sleep mode by itself when the conversion ends.
    if (!waitStatusClear(BME280_STATUS_MEASURING, POLL_TRIES)) return false;

    uint8_t data[BME280_DATA_SIZE];
    size_t size = humidity ? BME280_DATA_SIZE : BMP280_DATA_SIZE;
    Bme280Raw raw;
    return readRegisters(BME280_REG_DATA, data, size) && parseBme280Raw(data, size, raw) &&
           compensateBme280(calibration_, raw, humidity, out);
}

bool Bme280Sensor::measure(Bme280Reading& out) {
    if (!present()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    Bme280Reading reading;
    bool ok = measureLocked(reading);
    if (ok != !failing_) {
        if (ok) {
            ESP_LOGI(BME_TAG, "%s answers again", bme280ChipName(chip_));
        } else {
            ESP_LOGW(BME_TAG, "%s at 0x%02X gave no measurement", bme280ChipName(chip_), address_);
        }
        failing_ = !ok;
    }
    if (!ok) return false;
    last_ = reading;
    lastAtUs_ = std::max<int64_t>(esp_timer_get_time(), 1);
    out = reading;
    return true;
}

bool Bme280Sensor::lastReading(Bme280Reading& out, uint32_t& ageSeconds) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (lastAtUs_ == 0) return false;
    out = last_;
    ageSeconds = static_cast<uint32_t>((esp_timer_get_time() - lastAtUs_) / US_PER_S);
    return true;
}

TypedResult Bme280Sensor::measureInto(Fields& out) {
    enum Field : size_t { CHIP, ADDRESS, TEMPERATURE, PRESSURE, HUMIDITY, AGE };
    if (!present()) return TypedResult(SENSOR_ERR_NOT_FOUND);
    Bme280Reading reading;
    bool fresh = measure(reading);
    uint32_t ageSeconds = 0;
    if (!fresh && !lastReading(reading, ageSeconds)) {
        return TypedResult(SENSOR_ERR_NO_MEASUREMENT);
    }
    out.setU(CHIP, chip_ == Bme280Chip::BME280 ? 1 : 0);
    out.setBytes(ADDRESS, &address_, sizeof(address_));
    out.setI(TEMPERATURE, reading.tempCentiC);
    out.setU(PRESSURE, reading.pressurePa);
    if (reading.hasHumidity) out.setU(HUMIDITY, reading.rhCentiPercent);
    if (!fresh) out.setU(AGE, ageSeconds);
    return TypedResult();
}

#else

void Bme280Sensor::begin() {}

bool Bme280Sensor::measure(Bme280Reading&) {
    return false;
}

bool Bme280Sensor::lastReading(Bme280Reading&, uint32_t&) const {
    return false;
}

TypedResult Bme280Sensor::measureInto(Fields&) {
    return TypedResult(SENSOR_ERR_NO_SUPPORT);
}

#endif
