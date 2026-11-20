#include "reportInterval.h"

#include <algorithm>

namespace {

std::string trimmed(const std::string& text) {
    const char* const blanks = " \t\r\n";
    size_t first = text.find_first_not_of(blanks);
    if (first == std::string::npos) return "";
    return text.substr(first, text.find_last_not_of(blanks) - first + 1);
}

bool isDefaultWord(const std::string& text) {
    const std::string word = "default";
    return text.size() == word.size() &&
           std::equal(text.begin(), text.end(), word.begin(), [](char a, char b) {
               return (a >= 'A' && a <= 'Z' ? static_cast<char>(a - 'A' + 'a') : a) == b;
           });
}

}  // namespace

bool reportIntervalValid(uint32_t seconds) {
    return seconds >= REPORT_INTERVAL_MIN_S && seconds <= REPORT_INTERVAL_MAX_S;
}

bool parseReportInterval(const std::string& args, uint32_t& seconds) {
    const std::string text = trimmed(args);
    if (isDefaultWord(text)) {
        seconds = REPORT_INTERVAL_NONE;
        return true;
    }
    // Nine digits always fit in 32 bits.
    constexpr size_t MAX_DIGITS = 9;
    if (text.empty() || text.size() > MAX_DIGITS) return false;
    uint32_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<uint32_t>(c - '0');
    }
    if (value != REPORT_INTERVAL_NONE && !reportIntervalValid(value)) return false;
    seconds = value;
    return true;
}

ReportPacing chooseReportPacing(uint32_t defaultMs, uint32_t storedS, bool installActive,
                                uint32_t installIntervalMs) {
    ReportPacing pacing;
    pacing.stored = reportIntervalValid(storedS);
    pacing.configuredMs = pacing.stored ? storedS * 1000 : defaultMs;
    pacing.installShortens = installActive && installIntervalMs < pacing.configuredMs;
    pacing.intervalMs = pacing.installShortens ? installIntervalMs : pacing.configuredMs;
    return pacing;
}

std::string describeReportPacing(const ReportPacing& pacing, uint32_t nextInS) {
    const char* source = pacing.stored ? "stored" : "default";
    std::string text = "report every " + std::to_string(pacing.intervalMs / 1000) + " s (";
    if (pacing.installShortens) {
        text += "install mode, then " + std::to_string(pacing.configuredMs / 1000) + " s " + source;
    } else {
        text += source;
    }
    return text + "), next in " + std::to_string(nextInS) + " s";
}
