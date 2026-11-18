#include "monReport.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr uint64_t MS_PER_S = 1000;
constexpr float SNR_STEPS_PER_DB = 4.0F;

void putU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

void putU32(std::vector<uint8_t>& out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<uint8_t>(value >> shift));
}

uint16_t getU16(const uint8_t* in) {
    return static_cast<uint16_t>(in[0] | in[1] << 8);
}

uint32_t getU32(const uint8_t* in) {
    return static_cast<uint32_t>(in[0]) | static_cast<uint32_t>(in[1]) << 8 |
           static_cast<uint32_t>(in[2]) << 16 | static_cast<uint32_t>(in[3]) << 24;
}

template <typename Narrow, typename Wide>
Narrow saturate(Wide value) {
    return static_cast<Narrow>(std::min<Wide>(value, std::numeric_limits<Narrow>::max()));
}

uint8_t flagsOf(const MonReport& report) {
    uint8_t flags = 0;
    if (report.externalPower) flags |= MON_FLAG_EXTERNAL_POWER;
    if (report.charging) flags |= MON_FLAG_CHARGING;
    if (report.batteryPresent) flags |= MON_FLAG_BATTERY_PRESENT;
    if (report.hasSensor) flags |= MON_FLAG_SENSOR;
    if (report.installMode) flags |= MON_FLAG_INSTALL_MODE;
    return flags;
}

bool decodeV1(const uint8_t* payload, size_t size, MonReport& report) {
    if (size < MON_REPORT_HEADER_SIZE_V1) return false;
    uint32_t count = getU32(payload + 10);
    if (count > (size - MON_REPORT_HEADER_SIZE_V1) / MON_ROUTE_SIZE_V1) return false;
    if (size != MON_REPORT_HEADER_SIZE_V1 + MON_ROUTE_SIZE_V1 * count) return false;

    report = MonReport();
    report.version = 1;
    report.uptimeMs = getU32(payload + 2);
    report.txQueue = getU16(payload + 6);
    report.rxQueue = getU16(payload + 8);
    report.routes.reserve(count);
    const uint8_t* entry = payload + MON_REPORT_HEADER_SIZE_V1;
    for (uint32_t i = 0; i < count; i++, entry += MON_ROUTE_SIZE_V1) {
        MonRoute route;
        route.neighbor = getU16(entry);
        route.nextHop = getU16(entry + 2);
        route.linkQuality = entry[4];
        route.hopCount = entry[5];
        report.routes.push_back(route);
    }
    return true;
}

bool decodeV2(const uint8_t* payload, size_t size, MonReport& report) {
    if (size < MON_REPORT_HEADER_SIZE) return false;
    uint8_t flags = payload[2];
    bool hasSensor = (flags & MON_FLAG_SENSOR) != 0;
    size_t count = payload[13];
    size_t routesAt = MON_REPORT_HEADER_SIZE + (hasSensor ? MON_SENSOR_SIZE : 0);
    if (size != routesAt + MON_ROUTE_SIZE * count) return false;

    report = MonReport();
    report.version = 2;
    report.externalPower = (flags & MON_FLAG_EXTERNAL_POWER) != 0;
    report.charging = (flags & MON_FLAG_CHARGING) != 0;
    report.batteryPresent = (flags & MON_FLAG_BATTERY_PRESENT) != 0;
    report.installMode = (flags & MON_FLAG_INSTALL_MODE) != 0;
    report.hasSensor = hasSensor;
    report.uptimeMs = getU32(payload + 3) * MS_PER_S;
    report.txQueue = payload[7];
    report.rxQueue = payload[8];
    report.intervalS = getU16(payload + 9);
    report.battMv = getU16(payload + 11);
    if (hasSensor) {
        const uint8_t* block = payload + MON_REPORT_HEADER_SIZE;
        report.sensor.tempCentiC = static_cast<int16_t>(getU16(block));
        report.sensor.rhCentiPercent = getU16(block + 2);
        report.sensor.pressureDeciHpa = getU16(block + 4);
    }
    report.routes.reserve(count);
    const uint8_t* entry = payload + routesAt;
    for (size_t i = 0; i < count; i++, entry += MON_ROUTE_SIZE) {
        MonRoute route;
        route.neighbor = getU16(entry);
        route.nextHop = getU16(entry + 2);
        route.linkQuality = entry[4];
        route.hopCount = entry[5];
        route.rssi = static_cast<int8_t>(entry[6]);
        route.snrQuarterDb = static_cast<int8_t>(entry[7]);
        report.routes.push_back(route);
    }
    return true;
}

}  // namespace

std::vector<uint8_t> encodeMonReport(const MonReport& report) {
    size_t count = std::min(report.routes.size(), MON_MAX_ROUTES);
    std::vector<uint8_t> out;
    out.reserve(MON_REPORT_HEADER_SIZE + (report.hasSensor ? MON_SENSOR_SIZE : 0) +
                MON_ROUTE_SIZE * count);
    putU16(out, MON_REPORT_MARKER_V2);
    out.push_back(flagsOf(report));
    putU32(out, saturate<uint32_t, uint64_t>(report.uptimeMs / MS_PER_S));
    out.push_back(saturate<uint8_t, uint16_t>(report.txQueue));
    out.push_back(saturate<uint8_t, uint16_t>(report.rxQueue));
    putU16(out, report.intervalS);
    putU16(out, report.battMv);
    out.push_back(static_cast<uint8_t>(count));
    if (report.hasSensor) {
        putU16(out, static_cast<uint16_t>(report.sensor.tempCentiC));
        putU16(out, report.sensor.rhCentiPercent);
        putU16(out, report.sensor.pressureDeciHpa);
    }
    for (size_t i = 0; i < count; i++) {
        const MonRoute& route = report.routes[i];
        putU16(out, route.neighbor);
        putU16(out, route.nextHop);
        out.push_back(route.linkQuality);
        out.push_back(route.hopCount);
        out.push_back(static_cast<uint8_t>(route.rssi));
        out.push_back(static_cast<uint8_t>(route.snrQuarterDb));
    }
    return out;
}

std::vector<uint8_t> encodeMonReportV1(const MonReport& report) {
    std::vector<uint8_t> out;
    out.reserve(MON_REPORT_HEADER_SIZE_V1 + MON_ROUTE_SIZE_V1 * report.routes.size());
    putU16(out, MON_REPORT_MARKER);
    putU32(out, static_cast<uint32_t>(report.uptimeMs));
    putU16(out, report.txQueue);
    putU16(out, report.rxQueue);
    putU32(out, static_cast<uint32_t>(report.routes.size()));
    for (const MonRoute& route : report.routes) {
        putU16(out, route.neighbor);
        putU16(out, route.nextHop);
        out.push_back(route.linkQuality);
        out.push_back(route.hopCount);
    }
    return out;
}

bool decodeMonReport(const uint8_t* payload, size_t size, MonReport& report) {
    if (payload == nullptr || size < sizeof(uint16_t)) return false;
    switch (getU16(payload)) {
        case MON_REPORT_MARKER:
            return decodeV1(payload, size, report);
        case MON_REPORT_MARKER_V2:
            return decodeV2(payload, size, report);
        default:
            return false;
    }
}

size_t monRoutesPerMessage(size_t maxPayload, bool hasSensor) {
    size_t fixed = MON_REPORT_HEADER_SIZE + (hasSensor ? MON_SENSOR_SIZE : 0);
    if (maxPayload < fixed) return 0;
    return std::min((maxPayload - fixed) / MON_ROUTE_SIZE, MON_MAX_ROUTES);
}

std::vector<MonReport> splitMonReport(const MonReport& report, size_t maxPayload) {
    std::vector<MonReport> parts;
    if (maxPayload < MON_REPORT_HEADER_SIZE + (report.hasSensor ? MON_SENSOR_SIZE : 0)) {
        return parts;
    }
    MonReport state = report;
    state.routes.clear();
    size_t first = 0;
    do {
        size_t perMessage = monRoutesPerMessage(maxPayload, state.hasSensor);
        // A first part that only has room for the sensor block carries no route.
        if (perMessage == 0 && !state.hasSensor && first < report.routes.size()) return {};
        size_t count = std::min(perMessage, report.routes.size() - first);
        parts.push_back(state);
        auto begin = report.routes.begin() + static_cast<std::ptrdiff_t>(first);
        parts.back().routes.assign(begin, begin + static_cast<std::ptrdiff_t>(count));
        first += count;
        state.hasSensor = false;
    } while (first < report.routes.size());
    return parts;
}

int8_t monPackRssi(float dbm) {
    if (std::isnan(dbm) || dbm == 0.0F) return 0;
    return static_cast<int8_t>(std::lround(std::max(-127.0F, std::min(-1.0F, dbm))));
}

int8_t monPackSnr(float db) {
    if (std::isnan(db)) return 0;
    return static_cast<int8_t>(
        std::lround(std::max(-128.0F, std::min(127.0F, db * SNR_STEPS_PER_DB))));
}

float monSnrDb(int8_t quarterDb) {
    return static_cast<float>(quarterDb) / SNR_STEPS_PER_DB;
}

uint32_t monUptimeMs(const MonReport& report) {
    return saturate<uint32_t, uint64_t>(report.uptimeMs);
}
