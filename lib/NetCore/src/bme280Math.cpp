#include "bme280Math.h"

#include <algorithm>
#include <limits>

namespace {

constexpr uint8_t CHIP_ID_BME280 = 0x60;
constexpr uint8_t CHIP_ID_BMP280 = 0x58;
constexpr uint8_t CHIP_ID_BMP280_SAMPLE_A = 0x56;
constexpr uint8_t CHIP_ID_BMP280_SAMPLE_B = 0x57;
/** Output of a 20-bit channel that was not measured. */
constexpr uint32_t SKIPPED_20_BIT = 0x80000;
/** Humidity limits of the formula, in 1/4194304 %: 0 % and 100 %. */
constexpr int64_t HUMIDITY_MAX_Q22 = 419430400;

uint16_t u16le(const uint8_t* bytes) {
    return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
}

int16_t s16le(const uint8_t* bytes) {
    return static_cast<int16_t>(u16le(bytes));
}

uint32_t u20(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 12) | (static_cast<uint32_t>(bytes[1]) << 4) |
           (static_cast<uint32_t>(bytes[2]) >> 4);
}

/** @return the 12-bit two's complement value @p bits as a number. */
int16_t s12(uint32_t bits) {
    return static_cast<int16_t>(bits >= 0x800 ? static_cast<int32_t>(bits) - 0x1000
                                              : static_cast<int32_t>(bits));
}

/** Arithmetic shift right, as the formulas of the datasheet expect from >> on a signed value. */
int64_t asr(int64_t value, unsigned bits) {
    int64_t divisor = static_cast<int64_t>(1) << bits;
    int64_t quotient = value / divisor;
    return (value % divisor < 0) ? quotient - 1 : quotient;
}

// The formulas stay far inside 64 bits with the calibration of a real chip. The three helpers
// below wrap around on overflow, so the bytes of a broken chip give a wrong value and nothing
// worse.
int64_t wrapped(uint64_t value) {
    return value <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())
               ? static_cast<int64_t>(value)
               : -static_cast<int64_t>(~value) - 1;
}

int64_t mul(int64_t a, int64_t b) {
    return wrapped(static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}

int64_t add(int64_t a, int64_t b) {
    return wrapped(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}

/** @return t_fine, the temperature that the three formulas share. */
int64_t fineTemperature(const Bme280Calibration& c, uint32_t rawTemperature) {
    int64_t adc = rawTemperature;
    int64_t var1 = asr((asr(adc, 3) - static_cast<int64_t>(c.t1) * 2) * c.t2, 11);
    int64_t delta = asr(adc, 4) - c.t1;
    int64_t var2 = asr(asr(delta * delta, 12) * c.t3, 14);
    return var1 + var2;
}

/** @return the pressure in Pa, or false when the calibration would divide by zero. */
bool pressurePa(const Bme280Calibration& c, int64_t tFine, uint32_t rawPressure, uint32_t& out) {
    int64_t var1 = tFine - 128000;
    int64_t var2 = mul(var1 * var1, c.p6);
    var2 = add(var2, var1 * c.p5 * 131072);
    var2 = add(var2, static_cast<int64_t>(c.p4) * 34359738368LL);
    var1 = add(asr(mul(var1 * var1, c.p3), 8), var1 * c.p2 * 4096);
    var1 = asr(mul(add(140737488355328LL, var1), c.p1), 33);
    if (var1 == 0) return false;
    int64_t p = 1048576 - static_cast<int64_t>(rawPressure);
    p = mul(add(p * 2147483648LL, -var2), 3125);
    // The one quotient that does not fit is the lowest value divided by -1.
    if (var1 == -1) p = add(p, 1);
    p /= var1;
    var1 = asr(mul(c.p9, mul(asr(p, 13), asr(p, 13))), 25);
    var2 = asr(mul(c.p8, p), 19);
    p = add(asr(add(add(p, var1), var2), 8), static_cast<int64_t>(c.p7) * 16);
    // p is in 1/256 Pa.
    int64_t pa = asr(add(p, 128), 8);
    out = static_cast<uint32_t>(
        std::max<int64_t>(0, std::min<int64_t>(pa, std::numeric_limits<uint32_t>::max())));
    return true;
}

/** @return the relative humidity in 0.01 %. */
uint32_t humidityCentiPercent(const Bme280Calibration& c, int64_t tFine, uint32_t rawHumidity) {
    int64_t v = tFine - 76800;
    int64_t left =
        asr(static_cast<int64_t>(rawHumidity) * 16384 - static_cast<int64_t>(c.h4) * 1048576 -
                static_cast<int64_t>(c.h5) * v + 16384,
            15);
    int64_t right =
        asr((asr(asr(v * c.h6, 10) * (asr(v * c.h3, 11) + 32768), 10) + 2097152) * c.h2 + 8192, 14);
    v = mul(left, right);
    v = add(v, -asr(mul(asr(mul(asr(v, 15), asr(v, 15)), 7), c.h1), 4));
    v = std::max<int64_t>(0, std::min(v, HUMIDITY_MAX_Q22));
    // v >> 12 is in 1/1024 %.
    return static_cast<uint32_t>((asr(v, 12) * 100 + 512) / 1024);
}

}  // namespace

Bme280Chip bme280ChipFromId(uint8_t id) {
    switch (id) {
        case CHIP_ID_BME280:
            return Bme280Chip::BME280;
        case CHIP_ID_BMP280:
        case CHIP_ID_BMP280_SAMPLE_A:
        case CHIP_ID_BMP280_SAMPLE_B:
            return Bme280Chip::BMP280;
        default:
            return Bme280Chip::NONE;
    }
}

const char* bme280ChipName(Bme280Chip chip) {
    switch (chip) {
        case Bme280Chip::BME280:
            return "BME280";
        case Bme280Chip::BMP280:
            return "BMP280";
        case Bme280Chip::NONE:
            break;
    }
    return "none";
}

bool parseBme280Calibration(const uint8_t* tp, const uint8_t* h, Bme280Calibration& out) {
    Bme280Calibration c;
    c.t1 = u16le(tp);
    c.t2 = s16le(tp + 2);
    c.t3 = s16le(tp + 4);
    c.p1 = u16le(tp + 6);
    c.p2 = s16le(tp + 8);
    c.p3 = s16le(tp + 10);
    c.p4 = s16le(tp + 12);
    c.p5 = s16le(tp + 14);
    c.p6 = s16le(tp + 16);
    c.p7 = s16le(tp + 18);
    c.p8 = s16le(tp + 20);
    c.p9 = s16le(tp + 22);
    if (c.t1 == 0 || c.t1 == 0xFFFF || c.p1 == 0 || c.p1 == 0xFFFF) return false;
    if (h != nullptr) {
        c.h1 = tp[25];
        c.h2 = s16le(h);
        c.h3 = h[2];
        c.h4 = s12((static_cast<uint32_t>(h[3]) << 4) | (h[4] & 0x0FU));
        c.h5 = s12((static_cast<uint32_t>(h[5]) << 4) | (static_cast<uint32_t>(h[4]) >> 4));
        c.h6 = static_cast<int8_t>(h[6]);
    }
    out = c;
    return true;
}

bool parseBme280Raw(const uint8_t* data, size_t size, Bme280Raw& out) {
    if (size != BME280_DATA_SIZE && size != BMP280_DATA_SIZE) return false;
    Bme280Raw raw;
    raw.pressure = u20(data);
    raw.temperature = u20(data + 3);
    if (raw.pressure == SKIPPED_20_BIT || raw.temperature == SKIPPED_20_BIT) return false;
    if (size == BME280_DATA_SIZE) raw.humidity = (static_cast<uint32_t>(data[6]) << 8) | data[7];
    out = raw;
    return true;
}

bool compensateBme280(const Bme280Calibration& calibration, const Bme280Raw& raw, bool withHumidity,
                      Bme280Reading& out) {
    int64_t tFine = fineTemperature(calibration, raw.temperature);
    Bme280Reading reading;
    reading.tempCentiC = static_cast<int32_t>(asr(tFine * 5 + 128, 8));
    if (!pressurePa(calibration, tFine, raw.pressure, reading.pressurePa)) return false;
    reading.hasHumidity = withHumidity;
    if (withHumidity) {
        reading.rhCentiPercent = humidityCentiPercent(calibration, tFine, raw.humidity);
    }
    out = reading;
    return true;
}

uint32_t bme280MeasureTimeMs(bool withHumidity) {
    // 1.25 ms, plus 2.3 ms for the temperature, 2.3 + 0.575 ms for the pressure and the same
    // for the humidity: 6.4 ms and 9.3 ms.
    return withHumidity ? 10 : 7;
}

MonSensor bme280ToMonSensor(const Bme280Reading& reading) {
    MonSensor sensor;
    sensor.tempCentiC = static_cast<int16_t>(std::max<int32_t>(
        std::numeric_limits<int16_t>::min(),
        std::min<int32_t>(reading.tempCentiC, std::numeric_limits<int16_t>::max())));
    // 0xFFFF means that the sensor measures no humidity.
    sensor.rhCentiPercent =
        reading.hasHumidity
            ? static_cast<uint16_t>(std::min<uint32_t>(reading.rhCentiPercent, MON_NO_HUMIDITY - 1))
            : MON_NO_HUMIDITY;
    uint32_t deciHpa = reading.pressurePa / 10 + (reading.pressurePa % 10 >= 5 ? 1 : 0);
    sensor.pressureDeciHpa =
        static_cast<uint16_t>(std::min<uint32_t>(deciHpa, std::numeric_limits<uint16_t>::max()));
    return sensor;
}
