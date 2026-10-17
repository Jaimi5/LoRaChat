#include "otaReport.h"

constexpr size_t ReportBuilder::MAX_SIZE;
constexpr size_t ReportBuilder::MAX_TEXT;

std::vector<uint8_t> verdictId(const OtaRecord& record) {
    std::vector<uint8_t> id(record.attemptSha.begin(), record.attemptSha.end());
    id.push_back(static_cast<uint8_t>(record.lastOutcome));
    id.push_back(record.unexplainedRollbacks);
    return id;
}

const char* reportEventName(ReportEvent event) {
    switch (event) {
        case ReportEvent::NOOP:
            return "noop";
        case ReportEvent::WRITTEN:
            return "written";
        case ReportEvent::VALID:
            return "valid";
        case ReportEvent::ROLLBACK:
            return "rollback";
        case ReportEvent::FAILED:
            return "failed";
    }
    return "failed";
}

ReportBuilder::ReportBuilder() : json_("{\"v\":1") {}

void ReportBuilder::key(const char* name) {
    json_ += ",\"";
    json_ += name;
    json_ += "\":";
}

ReportBuilder& ReportBuilder::addText(const char* name, const std::string& value) {
    bool ok = !value.empty() && value.size() <= MAX_TEXT;
    for (char c : value) ok = ok && c > ' ' && c <= '~' && c != '"' && c != '\\';
    if (!ok) valid_ = false;
    key(name);
    json_ += "\"" + value + "\"";
    return *this;
}

ReportBuilder& ReportBuilder::addNumber(const char* name, int64_t value) {
    key(name);
    json_ += std::to_string(value);
    return *this;
}

ReportBuilder& ReportBuilder::addFlag(const char* name, bool value) {
    key(name);
    json_ += value ? "true" : "false";
    return *this;
}

std::string ReportBuilder::body() const {
    std::string body = json_ + "}";
    return valid_ && body.size() <= MAX_SIZE ? body : "";
}
