#include "otaReport.h"

#include <cstdio>

const char* reportEventName(ReportEvent event) {
    switch (event) {
        case ReportEvent::NOOP: return "noop";
        case ReportEvent::WRITTEN: return "written";
        case ReportEvent::VALID: return "valid";
        case ReportEvent::ROLLBACK: return "rollback";
        case ReportEvent::FAILED: return "failed";
    }
    return "noop";
}

namespace {

class JsonObject {
public:
    void raw(const char* name, const std::string& value) {
        out_ += out_.empty() ? '{' : ',';
        out_ += '"';
        out_ += name;
        out_ += "\":";
        out_ += value;
    }

    void text(const char* name, const std::string& value, bool always = false) {
        if (value.empty() && !always) return;
        raw(name, quoted(value));
    }

    void number(const char* name, int64_t value) {
        if (value == REPORT_ABSENT) return;
        raw(name, std::to_string(static_cast<long long>(value)));
    }

    std::string close() { return out_ + '}'; }

private:
    static std::string quoted(const std::string& value) {
        std::string out = "\"";
        for (char c : value) {
            if (c == '"' || c == '\\') {
                out += '\\';
                out += c;
            } else if (static_cast<unsigned char>(c) < 0x20) {
                char escaped[7];
                snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned char>(c));
                out += escaped;
            } else {
                out += c;
            }
        }
        return out + '"';
    }

    std::string out_;
};

}  // namespace

std::string encodeReport(const OtaReport& r) {
    char node[5];
    snprintf(node, sizeof(node), "%04X", r.node);

    JsonObject json;
    json.raw("v", "1");
    json.text("node", node);
    json.text("mac", r.mac);
    json.text("env", r.env, true);
    json.text("role", r.role);
    json.text("ver", r.ver, true);
    json.text("part", r.part);
    json.text("state", r.state);
    json.number("boot", r.boot);
    json.text("rr", r.rr);
    json.text("bl", r.bl);
    json.text("event", reportEventName(r.event));
    json.text("last_invalid", r.lastInvalid);
    json.number("status", r.status);
    json.number("decision", r.decision);
    json.number("reason", r.reason);
    json.text("sha", r.sha);
    json.number("rssi", r.rssi);
    json.number("dl_ms", r.dlMs);
    json.number("bytes", r.bytes);
    json.number("heap_free", r.heapFree);
    json.number("heap_min", r.heapMin);
    json.number("batt_mv", r.battMv);
    if (r.vbus >= 0) json.raw("vbus", r.vbus ? "true" : "false");
    json.number("uptime_s", r.uptimeS);
    return json.close();
}
