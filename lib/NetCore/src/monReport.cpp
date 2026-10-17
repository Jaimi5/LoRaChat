#include "monReport.h"

namespace {

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

}  // namespace

std::vector<uint8_t> encodeMonReport(const MonReport& report) {
    std::vector<uint8_t> out;
    out.reserve(MON_REPORT_HEADER_SIZE + MON_ROUTE_SIZE * report.routes.size());
    putU16(out, MON_REPORT_MARKER);
    putU32(out, report.uptimeMs);
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
    if (payload == nullptr || size < MON_REPORT_HEADER_SIZE) return false;
    if (getU16(payload) != MON_REPORT_MARKER) return false;
    uint32_t count = getU32(payload + 10);
    if (count > (size - MON_REPORT_HEADER_SIZE) / MON_ROUTE_SIZE) return false;
    if (size != MON_REPORT_HEADER_SIZE + MON_ROUTE_SIZE * count) return false;

    report.uptimeMs = getU32(payload + 2);
    report.txQueue = getU16(payload + 6);
    report.rxQueue = getU16(payload + 8);
    report.routes.clear();
    report.routes.reserve(count);
    const uint8_t* entry = payload + MON_REPORT_HEADER_SIZE;
    for (uint32_t i = 0; i < count; i++, entry += MON_ROUTE_SIZE) {
        MonRoute route;
        route.neighbor = getU16(entry);
        route.nextHop = getU16(entry + 2);
        route.linkQuality = entry[4];
        route.hopCount = entry[5];
        report.routes.push_back(route);
    }
    return true;
}

size_t monRoutesPerMessage(size_t maxPayload) {
    if (maxPayload < MON_REPORT_HEADER_SIZE) return 0;
    return (maxPayload - MON_REPORT_HEADER_SIZE) / MON_ROUTE_SIZE;
}
